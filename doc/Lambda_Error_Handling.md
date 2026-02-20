# Lambda Error Handling

This document covers Lambda's error handling system — how errors are created, returned, propagated, and enforced at compile time.

> **Related Documentation**:
> - [Lambda Reference](Lambda_Reference.md) — Language overview
> - [Lambda Functions](Lambda_Func.md) — Function declarations and return types
> - [Lambda Sys Func Reference](Lambda_Sys_Func.md) — Built-in system functions
> - [Lambda Type System](Lambda_Type.md) — Type annotations

---

## Table of Contents

1. [Overview](#overview)
2. [The `error` Type](#the-error-type)
3. [Creating Errors](#creating-errors)
4. [The `raise` Keyword](#the-raise-keyword)
5. [Error Return Types (`T^E`)](#error-return-types-te)
6. [Error Handling at Call Sites](#error-handling-at-call-sites)
7. [Error Propagation (`^` Operator)](#error-propagation--operator)
8. [Error Destructuring (`let a^err = expr`)](#error-destructuring-let-aerr--expr)
9. [Compile-Time Enforcement](#compile-time-enforcement)
10. [System Functions That Can Raise](#system-functions-that-can-raise)
11. [Error Code Categories](#error-code-categories)
12. [Checking for Errors](#checking-for-errors)
13. [Error Truthiness and Defaults](#error-truthiness-and-defaults)
14. [Examples](#examples)

---

## Overview

Lambda adopts an **error-as-return-value** paradigm. There is **no** `try`/`throw`/`catch` exception handling. Instead:

- Errors are explicit values that flow through the type system.
- Functions declare whether they can fail using the `T^E` return type syntax.
- The `raise` keyword is the only way to return an error from a function.
- The compiler **enforces** that callers handle errors — ignoring an error is a compile-time error.

This approach is inspired by Rust's `Result<T, E>` and Zig's `T!E`, but with lighter syntax.

---

## The `error` Type

Lambda has a built-in `error` type with the following fields:

| Field | Type | Description |
|-------|------|-------------|
| `code` | `int` | Error code (see [Error Code Categories](#error-code-categories)) |
| `message` | `string` | Human-readable error message |
| `file` | `string?` | Source file where the error occurred |
| `line` | `int` | 1-based line number |
| `column` | `int` | 1-based column number |
| `source` | `error?` | Wrapped/chained inner error |

### Accessing Error Fields

```lambda
let result^err = divide(10, 0)
if (^err) {
    print("code: " ++ str(err.code))
    print("message: " ++ err.message)
    if (err.source is error)
        print("caused by: " ++ err.source.message)
}
```

---

## Creating Errors

The `error()` function constructs error values:

```lambda
// Simple error with message (code defaults to 318 = user_error)
error("something went wrong")

// Error wrapping another error (for error chaining)
error("failed to load config", inner_err)

// Error with parameter map (full control)
error({
    code: 304,
    message: "division by zero"
})
```

**Constructor signatures:**

| Signature | Description |
|-----------|-------------|
| `error(message: string)` | Creates user error with message |
| `error(message: string, source: error)` | Creates error wrapping another error |
| `error(params: map)` | Creates error with explicit fields |

---

## The `raise` Keyword

`raise` is the **only** way to return an error from a function. It is distinct from `return`, which only returns normal values.

```lambda
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a / b
}
```

**Key properties:**

- `raise` immediately returns the error value to the caller.
- A function must declare an error return type (`T^` or `T^E`) to use `raise`.
- Using `raise` in a function with a plain `T` return type is a compile error.

```lambda
// ❌ Compile error: function does not declare error return
fn pure_add(a, b) int {
    raise error("oops")
}
```

---

## Error Return Types (`T^E`)

Functions declare whether they can fail using the `^` suffix on their return type:

| Syntax | Meaning |
|--------|---------|
| `T` | Always succeeds — no error possible |
| `T^` | May return `T` or any error (shorthand for `T^error`) |
| `T^E` | May return `T` or a specific error type `E` |
| `T^E1 \| E2` | May return `T` or one of multiple error types |

```lambda
// Always succeeds
fn add(a: int, b: int) int => a + b

// May fail with any error
fn parse(s: string) int^ { ... }

// May fail with a specific error type
fn divide(a: int, b: int) int^DivisionError { ... }

// May fail with multiple error types
fn load(path: string) Config ^ ParseError | IOError { ... }
```

> **Note:** The error type after `^` is restricted to `error`, identifiers, or unions of these. Complex type expressions (maps, arrays) are not allowed — define a named type instead.

### Error Union in Parameters and Let Bindings

The `T^` syntax also works in parameter types and let bindings:

```lambda
// Parameter that accepts a value-or-error
fn process(input: int^) int { ... }

// Let binding that may hold a value-or-error
let result: int^ = may_fail(x)
```

---

## Error Handling at Call Sites

When calling a function that returns `T^`, the caller **must** handle the error in one of the allowed ways:

| Pattern | Meaning | Allowed? |
|---------|---------|----------|
| `let a = F()^` | Propagate error, bind unwrapped value | ✅ |
| `let a^err = F()` | Capture value and error explicitly | ✅ |
| `F()^` | Propagate error, discard value | ✅ |
| `let a = F()` | **Ignoring error** | ❌ Compile error |
| `F()` | **Ignoring error and value** | ❌ Compile error |

---

## Error Propagation (`^` Operator)

The `^` postfix operator on a call expression unwraps the success value or propagates the error to the caller:

```lambda
fn compute(x: int) int^ {
    let a = parse(input)^      // if error, return it immediately
    let b = divide(a, x)^      // if error, return it immediately
    a + b                       // normal return
}
```

**Semantics:**
- If the call returns an error, `^` immediately returns that error from the enclosing function.
- If the call succeeds, the expression evaluates to the unwrapped success value.

**Rules:**
- `^` is only valid on calls to functions that can raise errors.
- Using `^` on a non-error-returning function is a compile error:
  ```lambda
  let x = add(1, 2)^  // ❌ Compile error: 'add' does not return errors
  ```
- The enclosing function must itself declare an error return type to use `^`.

---

## Error Destructuring (`let a^err = expr`)

The `^` in a let binding captures both the success value and the error into separate variables:

```lambda
let result^err = divide(10, x)
if (^err) {
    print("error: " ++ err.message)
    0  // default value
} else {
    result * 2
}
```

**Semantics:**
- If the expression returns an error: the value variable (`result`) is `null`, the error variable (`err`) holds the error.
- If the expression succeeds: the value variable holds the value, the error variable is `null`.
- Use `^err` (prefix `^`) to test whether the error variable is an error (see [Checking for Errors](#checking-for-errors)).

This is the primary way to **handle** an error locally rather than propagating it.

---

## Compile-Time Enforcement

Lambda **refuses to compile** code that ignores errors. This is a language rule, not a warning.

### Why?

Go's lack of enforcement is widely criticized — ignored errors cause production bugs. Rust's `#[must_use]` is a warning that can be suppressed. Lambda and Zig take the strongest stance: the compiler enforces it.

### Rules

1. **Error-returning function calls must be handled**
   ```lambda
   divide(10, 0)           // ❌ compile error: unhandled error return
   let x = divide(10, 0)   // ❌ compile error: unhandled error return
   ```

2. **Functions with plain `T` return type cannot contain `raise`**
   ```lambda
   fn pure_add(a, b) int {
       raise error("oops")  // ❌ compile error
   }
   ```

3. **`^` is only valid on error-returning calls**
   ```lambda
   let x = add(1, 2)^  // ❌ compile error: 'add' does not return errors
   ```

### Comparison Across Languages

| Aspect | **Go** | **Rust** | **Zig** | **Lambda** |
|--------|--------|----------|---------|------------|
| **Can ignore error?** | ✅ Yes | ⚠️ Warning (`#[must_use]`) | ❌ Compile error | ❌ Compile error |
| **Propagation syntax** | Manual `if err != nil` | `f()?` | `try f()` | `f()^` |
| **Destructure error** | `val, err := f()` | `match` on `Result` | `catch \|err\|` | `let val^err = f()` |
| **Error type** | `(T, error)` tuple | `Result<T, E>` enum | `T!E` error union | `T^E` |
| **Type preserved?** | ⚠️ Interface (erased) | ✅ Static | ✅ Static | ✅ Static |
| **Enforcement** | Convention only | Lint attribute | Language rule | Language rule |

---

## System Functions That Can Raise

The following built-in functions perform I/O and may fail. They enforce the same error handling rules as user-defined `T^` functions:

### Pure I/O Functions

| Function | Description |
|----------|-------------|
| `input(source)` | Read and parse data from file or URL |
| `input(source, format)` | Read with explicit format |

### Procedural I/O Functions

| Function | Description |
|----------|-------------|
| `output(data, target)` | Write data to file |
| `output(data, target, options)` | Write with format/options |
| `cmd(command)` | Execute shell command |
| `io.copy(src, dst)` | Copy file or directory |
| `io.move(src, dst)` | Move/rename file or directory |
| `io.delete(path)` | Delete file or directory |
| `io.mkdir(path)` | Create directory (recursive) |
| `io.touch(path)` | Create file or update timestamp |
| `io.symlink(target, link)` | Create symbolic link |
| `io.chmod(path, mode)` | Change file permissions |
| `io.rename(old, new)` | Rename file or directory |
| `io.fetch(url)` | Fetch content from URL |
| `io.fetch(url, options)` | Fetch with options |

### Example: Handling System Function Errors

```lambda
// ❌ Compile error: unhandled error from 'input'
let data = input("file.json")^

// ✅ Propagate error
let data = input("file.json")^

// ✅ Capture error explicitly
let data^err = input("file.json")

// ❌ Compile error: unhandled error from 'io.mkdir'
io.mkdir("output")

// ✅ Propagate error
io.mkdir("output")^
```

---

## Error Code Categories

Errors are categorized by numeric code ranges:

| Range | Category | Description |
|-------|----------|-------------|
| **1xx** | Syntax Errors | Lexical and grammatical errors during parsing |
| **2xx** | Semantic Errors | Type checking, compilation errors |
| **3xx** | Runtime Errors | Execution-time failures |
| **4xx** | I/O Errors | File, network, external resource errors |
| **5xx** | Internal Errors | Unexpected internal states |

### Common Error Codes

| Code | Name | Description |
|------|------|-------------|
| 201 | `type_mismatch` | Type incompatibility |
| 202 | `undefined_variable` | Reference to undefined variable |
| 203 | `undefined_function` | Reference to undefined function |
| 206 | `argument_count_mismatch` | Wrong number of arguments |
| 228 | `unhandled_error` | Error from can-raise function not handled |
| 301 | `null_reference` | Null dereference |
| 302 | `index_out_of_bounds` | Array/list index out of range |
| 304 | `division_by_zero` | Division or modulo by zero |
| 318 | `user_error` | User-defined error via `error()` |
| 401 | `file_not_found` | File does not exist |
| 402 | `file_access_denied` | Permission denied |
| 407 | `parse_error` | Error parsing input format |

User-created errors (via `error("message")`) default to code 318 (`user_error`).

---

## Checking for Errors

There are two ways to test whether a value is an error:

### `^expr` — Error Check Shorthand

The `^` prefix operator is the idiomatic way to check for errors. It returns `true` if the operand is an error, `false` otherwise:

```lambda
let result^err = divide(10, x)
if (^err) {
    print("Error: " ++ err.message)
}
```

This is especially concise with error destructuring:

```lambda
let data^err = input("file.json")
if (^err) { raise error("failed to load", err) }
```

### `expr is error` — Type Check

The `is` operator also works for error testing, and reads more naturally in some contexts:

```lambda
let result = some_operation()
if (result is error) {
    print("Error occurred: " ++ result.message)
}
```

`^expr` is equivalent to `expr is error` — use whichever reads better in context.

---

## Error Truthiness and Defaults

Error values are **falsy** in Lambda. This means:

- `if (err)` treats errors the same as `null` or `false` — the condition is **not** entered.
- Use `^err` (not bare `err`) to check for errors in conditions.

Because errors are falsy, the `or` operator provides a natural **default value** pattern:

```lambda
// If divide returns an error, fall through to 0
let safe_result = divide(10, x) or 0

// Chain with error destructuring for logging + default
let result^err = parse(input)
if (^err) { print("Warning: " ++ err.message) }
let value = result or default_value
```

### Why Errors Are Falsy

| Pattern | With falsy errors | With truthy errors |
|---------|-------------------|--------------------|
| `err or default` | ✅ Falls through to default | ❌ Returns the error |
| `if (^err)` | ✅ Explicit error check | — |
| `if (err)` | Treats error like null | Would enter the branch |

Falsy errors enable the `or` default idiom and prevent accidental use of error values as truthy conditions. The `^` prefix provides an explicit, lightweight error check.

---

## Examples

### Basic Error Handling

```lambda
fn may_fail(x) int^ {
    if (x < 0) raise error("negative input")
    else x * 2
}

// Propagate with ^
may_fail(5)^

// Destructure to handle locally
let result^err = may_fail(-1)
if (^err) {
    print("Got error: " ++ err.message)  // "negative input"
}
```

### Chaining Calls with `^`

```lambda
fn compute(x) int^ {
    let doubled = may_fail(x)^    // propagate if error
    doubled + 10                   // normal return
}

compute(5)^   // 20
```

### File Processing

```lambda
fn process_file(path: string) ProcessedData^ {
    let content = input(path, 'text)^
    let lines = split(content, "\n")
    let parsed = (for line in lines parse_line(line)^)
    aggregate(parsed)
}
```

### Error Wrapping

```lambda
fn load_config(path: string) Config^ {
    let content^file_err = input(path, 'text)
    if (^file_err)
        raise error("failed to read config file", file_err)

    let parsed^parse_err = input(content, 'json)
    if (^parse_err)
        raise error("invalid JSON in config", parse_err)

    parsed
}
```

### Procedural Script with Error Handling

```lambda
pn main() {
    let config = input("config.json", 'json)^

    for item in config.items {
        let result^err = process(item)
        if (^err) {
            print("Warning: " ++ err.message)
            continue
        }
        output(result, "output/" ++ item.name ++ ".json")^
    }

    print("Done")
}
```

---

## Summary

| Concept | Syntax | Purpose |
|---------|--------|---------|
| Declare error return | `fn F() T^` or `fn F() T^E` | Function may fail |
| Create error | `error("message")` | Construct error value |
| Raise error | `raise error("...")` | Return error from function |
| Propagate error | `F()^` | Unwrap or auto-return error |
| Capture error | `let val^err = F()` | Destructure into value + error |
| Check for error | `^err` or `x is error` | Test if value is an error |
| Default on error | `expr or default` | Errors are falsy, fall through to default |

Lambda's error handling provides **Go's simplicity** (errors as values), **Rust's safety** (`^` propagation, type enforcement), and **Zig's strictness** (compiler-enforced handling) — with concise, readable syntax.
