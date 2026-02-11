# Lambda Script Language Reference

## Table of Contents

1. [Introduction](#introduction)
2. [Language Overview](#language-overview)
3. [Documentation Guide](#documentation-guide)
4. [Modules and Imports](#modules-and-imports)
5. [I/O Module](#io-module)
6. [Error Handling](#error-handling)
7. [Examples](#examples)
8. [Language Philosophy](#language-philosophy-and-design-principles)

---

## Introduction

Lambda Script is a **general-purpose, cross-platform, pure functional scripting language** designed for data processing and document presentation. Built from scratch in C with JIT compilation using MIR (Medium Internal Representation) and reference counting memory management.

### Key Features

- **Pure Functional**: Immutable data structures and functional programming paradigms
- **JIT Compilation**: Near-native performance through MIR-based compilation
- **Cross-Platform**: Runs on macOS, Linux, Windows with consistent behavior
- **Rich Type System**: Strong typing with type inference and advanced type constructs
- **Document Processing**: Built-in support for 12+ input formats and multiple output formats
- **Unicode Support**: Configurable Unicode support with ICU integration
- **Memory Safe**: Reference counting and pool-based memory management

---

## Language Overview

Lambda Script is designed around functional programming principles with modern syntax. Programs consist of expressions that evaluate to values, with support for:

- Immutable data structures (lists, arrays, maps, elements)
- First-class functions and closures
- Pattern matching and destructuring
- Comprehensive type system with inference
- Built-in document processing capabilities

### Philosophy

1. **Data as Code**: Documents and data structures are first-class citizens
2. **Type Safety**: Compile-time type checking prevents runtime errors
3. **Expressiveness**: Concise syntax for complex data transformations
4. **Performance**: JIT compilation for production-ready performance

---

## Documentation Guide

The Lambda language documentation is organized into focused sub-documents for easier navigation and maintenance:

### Core Documentation

| Document | Description |
|----------|-------------|
| **[Lambda_Syntax.md](Lambda_Syntax.md)** | **Syntax Fundamentals** — Comments, identifiers, names, symbols, namespaces |
| **[Lambda_Data.md](Lambda_Data.md)** | **Literals and Collections** — Primitive types, path literals, arrays, lists, maps, elements, ranges, and data composition expressions |
| **[Lambda_Type.md](Lambda_Type.md)** | **Type System** — First-class types, type hierarchy, union types, function types, type patterns, and string patterns |
| **[Lambda_Expr_Stam.md](Lambda_Expr_Stam.md)** | **Expressions and Statements** — Arithmetic, comparisons, logical operations, pipe expressions, control flow, and operators |
| **[Lambda_Func.md](Lambda_Func.md)** | **Functions** — Function declarations, parameters, closures, higher-order functions, and procedural functions (`fn` and `pn`) |
| **[Lambda_Error_Handling.md](Lambda_Error_Handling.md)** | **Error Handling** — Error types, `raise` keyword, `?` propagation, `let a^err` destructuring, compile-time enforcement |

### Reference Documentation

| Document | Description |
|----------|-------------|
| **[Lambda_Sys_Func.md](Lambda_Sys_Func.md)** | **System Functions** — Complete reference for all built-in functions (type, math, string, collection, I/O, date/time) |
| **[Lambda_Validator_Guide.md](Lambda_Validator_Guide.md)** | **Validation** — Schema-based validation for data structures |

### Quick Reference

#### Data Types (see [Lambda_Data.md](Lambda_Data.md))

| Type | Description | Example |
|------|-------------|---------|
| `int` | 56-bit signed integer | `42`, `-123` |
| `float` | 64-bit floating point | `3.14`, `1e-10` |
| `string` | UTF-8 text | `"hello"` |
| `symbol` | Interned identifier | `'json` |
| `bool` | Boolean | `true`, `false` |
| `path` | File path or URL | `/etc.hosts`, `https.api.com` |
| `list` | Immutable tuple | `(1, 2, 3)` |
| `array` | Mutable array | `[1, 2, 3]` |
| `map` | Key-value mapping | `{name: "Alice"}` |
| `element` | Markup element | `<div; "content">` |

#### Type System (see [Lambda_Type.md](Lambda_Type.md))

```lambda
// Type annotations
let x: int = 42
let items: [string] = ["a", "b"]

// Union types
int | string           // Either int or string
int?                   // Nullable (int | null)

// Type declarations
type User = {name: string, age: int}
type HttpMethod = "GET" | "POST" | "PUT" | "DELETE"
```

#### Expressions (see [Lambda_Expr_Stam.md](Lambda_Expr_Stam.md))

```lambda
// Pipe expressions
[1, 2, 3] | ~ * 2              // [2, 4, 6]
users | ~.name where len(~) > 3  // Filter and transform

// For expressions
(for (x in [1,2,3] where x > 1 order by x desc) x * 2)

// If expressions
if (x > 0) "positive" else "negative"
```

#### Functions (see [Lambda_Func.md](Lambda_Func.md))

```lambda
// Pure function
fn add(a: int, b: int) => a + b

// Procedural function
pn save(data) {
    data |> "/tmp/output.json"
}

// Closures
fn make_adder(n: int) => (x) => x + n
```

---

## Modules and Imports

### Import Statements

```lambda
// Import modules
import module_name;
import .relative_module;        // Relative import
import \\windows\\style\\path;  // Windows-style path

// Import with alias
import alias: module_name;
import my_utils: .utilities;

// Multiple imports
import module1, module2, alias: module3;
```

### Module Structure

Each Lambda Script file is a module that can export public declarations. Currently only variable and function definitions can be exported:

```lambda
// math_utils.ls
pub PI = 3.14159;
pub E = 2.71828;

pub fn square(x: float) => x * x;
pub fn cube(x: float) => x * x * x;

// Private variable (not exported)
let v = 123;

// Private function (not exported)
fn helper(x: float) => x + 1;
```

### Using Imported Modules

```lambda
// main.ls
import math: .math_utils;

let area = math.PI * math.square(radius);
let volume = (4.0 / 3.0) * math.PI * math.cube(radius);
```

---

## I/O Module

Lambda provides a unified I/O system that handles both local files and remote URLs transparently.

### Pure I/O Functions

Available anywhere:

| Function | Description |
|----------|-------------|
| `input(source, format?)` | Read and parse data from file or URL |
| `exists(path)` | Check if file/directory exists |
| `format(data, format)` | Convert data to string format |

```lambda
// Read data
let data = input("config.json", 'json)
let html = input(https.example.com.page, 'html)

// Check existence
if exists(.config.json) { ... }
```

### Procedural I/O Functions

Available only in `pn` functions:

| Function | Description |
|----------|-------------|
| `io.copy(src, dst)` | Copy file/directory (supports URL sources) |
| `io.move(src, dst)` | Move/rename file or directory |
| `io.delete(path)` | Delete file or directory |
| `io.mkdir(path)` | Create directory (recursive) |
| `io.touch(path)` | Create file or update timestamp |
| `io.symlink(target, link)` | Create symbolic link |
| `io.chmod(path, mode)` | Change file permissions |
| `io.fetch(url, options?)` | Fetch content from URL |

```lambda
pn setup_project() {
    io.mkdir("./output/reports")
    io.copy("https://example.com/template.json", "./config.json")
    {initialized: true} |> "./output/.ready"
}
```

### Pipe Output Operators

Write data to files in procedural functions:

```lambda
pn export_data(data) {
    // Write (truncate)
    data |> "/tmp/output.json"

    // Append
    {event: "saved"} |>> "/tmp/events.log"
}
```

### Supported Formats

| Format | Extensions | Symbol |
|--------|------------|--------|
| JSON | `.json` | `'json` |
| YAML | `.yaml`, `.yml` | `'yaml` |
| XML | `.xml` | `'xml` |
| HTML | `.html` | `'html` |
| Markdown | `.md` | `'markdown` |
| CSV | `.csv` | `'csv` |
| TOML | `.toml` | `'toml` |
| Plain text | `.txt` | `'text` |

---

## Error Handling

Lambda uses an **error-as-return-value** paradigm — no `try`/`throw`/`catch` exceptions. Functions declare error return types with `T^E` syntax, raise errors with the `raise` keyword, and callers must explicitly handle errors using the `?` propagation operator or `let a^err` destructuring. Ignoring an error is a **compile-time error**.

```lambda
// Function that may fail
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a / b
}

// Propagate error with ?
let result = divide(10, x)?

// Or destructure to handle locally
let result^err = divide(10, x)
if (err != null) {
    print("error: " ++ err.message)
}
```

> **Full documentation**: See **[Lambda_Error_Handling.md](Lambda_Error_Handling.md)** for the complete guide — error types, `raise`, `?` operator, destructuring, enforcement rules, error codes, and examples.

---

## Examples

### Basic Data Processing

```lambda
// Read and process JSON data
let data = input("sales.json", 'json);

// Calculate total sales
let total = data.sales | ~.amount | sum;

// Filter high-value sales
let high_value = data.sales where ~.amount > 1000;

// Generate report
let report = {
    total_sales: total,
    high_value_count: len(high_value),
    average: total / len(data.sales),
    timestamp: datetime()
};

print(format(report, 'json));
```

### Document Processing

```lambda
// Parse Markdown document
let doc = input("article.md", 'markdown);

// Extract headings
let headings = doc where ~.tag == 'h1 or ~.tag == 'h2 | ~.content;

// Generate table of contents
let toc = <div class: "toc";
    <h2; "Table of Contents">
    <ul;
        for (heading in headings) <li; <a href: "#" ++ heading; heading>>
    >
>;

print(format(toc, 'html));
```

### Mathematical Computation

```lambda
// Recursive functions
fn factorial(n: int) int {
    if (n <= 1) 1
    else n * factorial(n - 1)
}

fn fibonacci(n: int) int {
    if (n <= 1) n
    else fibonacci(n - 1) + fibonacci(n - 2)
}

// Generate sequences
let factorials = (for (i in 1 to 10) factorial(i));
let fibs = (for (i in 1 to 15) fibonacci(i));

print("Factorials:", factorials)
print("Fibonacci:", fibs)
```

### Procedural Script with Main

```lambda
// script.ls - Run with: lambda.exe run script.ls

pn main() {
    print("Starting processing...")

    // Load configuration
    let config = if exists(.config.json) {
        input(.config.json, 'json)
    } else {
        {default: true}
    }

    // Process data
    var count = 0
    for item in config.items or [] {
        process_item(item)
        count = count + 1
    }

    // Save results
    {processed: count, time: now()} |> "./output/summary.json"

    print("Done! Processed", count, "items")
}

pn process_item(item) {
    // Processing logic here
    print("Processing:", item.name)
}
```

---

## Language Philosophy and Design Principles

### Functional Programming

Lambda Script embraces functional programming principles:

1. **Immutability**: Data structures are immutable by default
2. **Pure Functions**: Functions have no side effects (except I/O functions)
3. **Expression-Oriented**: Everything is an expression that returns a value
4. **Higher-Order Functions**: Functions are first-class values

### Type Safety

Strong typing prevents runtime errors:

1. **Static Type Checking**: Types are checked at compile time
2. **Type Inference**: Types are automatically inferred when possible
3. **Explicit Types**: Optional type annotations for clarity
4. **Error Types**: Errors are explicit values, not exceptions

### Performance

JIT compilation provides excellent performance:

1. **MIR Backend**: Uses MIR for efficient code generation
2. **Memory Pools**: Efficient memory allocation strategies
3. **Reference Counting**: Automatic memory management without GC pauses
4. **Structural Sharing**: Efficient copying of immutable data

### Expressiveness

Concise syntax for complex operations:

1. **Collection Comprehensions**: Powerful for-expressions for data processing
2. **Pipe Expressions**: Fluent data transformation pipelines
3. **Pattern Matching**: Type-based pattern matching with `is`
4. **Document Processing**: Built-in support for markup and data formats

---

## Further Reading

- **[Lambda_Syntax.md](Lambda_Syntax.md)** — Syntax fundamentals, names, symbols, and namespaces
- **[Lambda_Data.md](Lambda_Data.md)** — Complete guide to literals and collections
- **[Lambda_Type.md](Lambda_Type.md)** — Deep dive into the type system
- **[Lambda_Expr_Stam.md](Lambda_Expr_Stam.md)** — All expressions and operators
- **[Lambda_Func.md](Lambda_Func.md)** — Function features and patterns
- **[Lambda_Error_Handling.md](Lambda_Error_Handling.md)** — Error types, propagation, and enforcement
- **[Lambda_Sys_Func.md](Lambda_Sys_Func.md)** — All system functions

For the latest updates and examples, refer to the test files in the `test/lambda/` directory.
