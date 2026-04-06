# Lambda Script Language Reference

## Table of Contents

1. [Introduction](#introduction)
2. [Language Overview](#language-overview)
3. [Documentation Guide](#documentation-guide)
4. [Modules and Imports](#modules-and-imports)
5. [Error Handling](#error-handling)
6. [Examples](#examples)
7. [Language Philosophy](#language-philosophy-and-design-principles)

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

- Immutable data structures (arrays, maps, elements)
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
| **[Lambda_Data.md](Lambda_Data.md)** | **Literals and Collections** — Primitive types, path literals, arrays, maps, elements, ranges, and data composition expressions |
| **[Lambda_Type.md](Lambda_Type.md)** | **Type System** — First-class types, type hierarchy, union types, function types, type patterns, and string patterns |
| **[Lambda_Expr_Stam.md](Lambda_Expr_Stam.md)** | **Expressions and Statements** — Arithmetic, comparisons, logical operations, pipe expressions, query expressions (`?` `.?` `[T]`), control flow, and operators |
| **[Lambda_Func.md](Lambda_Func.md)** | **Functions** — Function declarations, parameters, closures, higher-order functions, and procedural functions (`fn` and `pn`) |
| **[Lambda_Procedural.md](Lambda_Procedural.md)** | **Procedural Programming** — Mutable variables, assignment, while loops, I/O module, `pn` functions, and `main()` entry point |
| **[Lambda_Error_Handling.md](Lambda_Error_Handling.md)** | **Error Handling** — Error types, `raise` keyword, `^` propagation, `let a^err` destructuring, compile-time enforcement |

### Reference Documentation

| Document | Description |
|----------|-------------|
| **[Lambda_CLI.md](Lambda_CLI.md)** | **CLI Reference** — Commands, flags, and usage for the Lambda command-line interface |
| **[Lambda_Sys_Func.md](Lambda_Sys_Func.md)** | **System Functions** — Complete reference for all built-in functions (type, math, string, collection, I/O, date/time) |
| **[Lambda_Validator_Guide.md](Lambda_Validator_Guide.md)** | **Validation** — Schema-based validation for data structures |

### Developer Documentation

| Document | Description |
|----------|-------------|
| **[Developer_Guide.md](dev/Developer_Guide.md)** | **Developer Guide** — Build from source, dependencies, testing, Tree-sitter grammar, MIR JIT |
| **[Lamdba_Runtime.md](dev/Lamdba_Runtime.md)** | **Lambda Runtime** — Runtime internals and architecture |

### Quick Reference

#### Data Types (see [Lambda_Data.md](Lambda_Data.md))

| Type      | Description                      | Example                       |
| --------- | -------------------------------- | ----------------------------- |
| `int`     | 56-bit signed integer            | `42`, `-123`                  |
| `float`   | 64-bit floating point            | `3.14`, `1e-10`               |
| `i8` `i16` `i32` | Sized signed integers   | `42i8`, `1000i16`, `100i32`   |
| `u8` `u16` `u32` | Sized unsigned integers | `255u8`, `60000u16`           |
| `i64`     | Alias for `int64`                | `100i64`                      |
| `u64`     | 64-bit unsigned integer          | `1000u64`                     |
| `f16` `f32` | Sized floating point           | `0.5f16`, `3.14f32`           |
| `f64`     | Alias for `float`                | `2.7f64`                      |
| `string`  | UTF-8 text                       | `"hello"`                     |
| `symbol`  | Interned identifier              | `'json'`                       |
| `bool`    | Boolean                          | `true`, `false`               |
| `path`    | File path or URL                 | `/etc.hosts`, `https.api.com` |
| `array`   | Ordered collection                   | `[1, 2, 3]`                   |
| `int[]`   | Typed int array                  | `var a: int[] = [1, 2]`       |
| `float[]` | Typed float array                | `var b: float[] = [0.1]`      |
| `map`     | Key-value mapping                | `{name: "Alice"}`             |
| `object`  | Nominally-typed map with methods | `{Point x: 1, y: 2}`          |
| `element` | Markup element                   | `<div "content">`             |

#### Type System (see [Lambda_Type.md](Lambda_Type.md))

```lambda
// Type annotations
let x: int = 42
let items: string[] = ["a", "b"]

// Sized numeric type annotations
let a: i8 = 42i8
let b: u32 = 255u32
let c: f32 = 3.14f32

// Typed array annotations
var arr: int[] = [1, 2, 3]     // Native int array
var data: float[] = [0.1, 0.2] // Native float array

// Union types
int | string           // Either int or string
int?                   // Nullable (int | null)

// Type declarations
type User = {name: string, age: int}
type HttpMethod = "GET" | "POST" | "PUT" | "DELETE"

// Object types (nominally-typed maps with methods)
type Point {
    x: float, y: float;
    fn distance(other: Point) => math.sqrt((x - other.x)**2 + (y - other.y)**2)
}
type Circle : Point { radius: float; }   // Inheritance
let p = {Point x: 3.0, y: 4.0}           // Object literal
p.distance({Point x: 0.0, y: 0.0})       // Method call
p is Point                                // true (nominal)
```

#### Expressions (see [Lambda_Expr_Stam.md](Lambda_Expr_Stam.md))

```lambda
// Pipe expressions
[1, 2, 3] | ~ * 2              // [2, 4, 6]
users | ~.name that (len(~) > 3) // Filter and transform

// Pipe/filter spread in array literals
[1, [2, 3] | ~, 4, 5]          // [1, 2, 3, 4, 5]
[0, items that (~ > 3), 9]      // flattened into enclosing array

// Query expressions — type-based search
html?<img>                       // all <img> at any depth
html?<div class: string>         // <div> with class attribute
data?int                         // all int values in tree
div.?<div>                       // self-inclusive query

// Child-level query — direct children only (no recursion)
type body = <body>
el[element]                      // direct child elements
el[string]                       // attr values + text children
html[body]?<p>                   // child then recursive

// For expressions
(for (x in [1,2,3] where x > 1 order by x desc) x * 2)

// If expressions
if (x > 0) "positive" else "negative"
if x > 0 { compute(x) } else "default"   // block form, expr else

// String patterns (see Lambda_Type.md § String Patterns)
string digits = \d+
string email = \w+ "@" \w+ "." \a[2,6]
"123" is digits                  // true (full-match)
match input {
    case digits: "number"
    default: "other"
}

// Pattern-aware string functions
find("a1b22", digits)            // [{value: "1", index: 1}, {value: "22", index: 3}]
replace("a1b2", digits, "N")    // "aNbN"
split("a1b2", digits)           // ["a", "b", ""]
```

#### Functions (see [Lambda_Func.md](Lambda_Func.md))

```lambda
// Pure function
fn add(a: int, b: int) => a + b

// Procedural function with mutation
pn process() {
    var x = 42
    x = 3.14             // type widening (int → float)
    let obj = {a: 1}
    obj.a = "hello"       // map field type change
}

// Typed array parameters for native array access
pn advance(pos: float[], vel: float[], dt: float) {
    for i in range(0, len(pos)) {
        pos[i] = pos[i] + vel[i] * dt   // native float ops
    }
}

// Closures with mutable captures
pn main() {
    var count = 0
    let inc = fn() { count = count + 1; count }
    print(inc())   // 1 (closure's own copy)
}
```

---

## Modules and Imports

### Import Statements

```lambda
// Relative import — resolved relative to the importing script's directory
import .relative_module
import .path.to.module

// Absolute import — resolved relative to CWD/project root
import module_name

// Import with alias
import alias: .module
import my_utils: .utilities
```

**Import resolution:**
- `.module` (dot prefix) — resolved relative to the importing script's directory. Nested imports work correctly: if `A.ls` imports `B.ls` and `B.ls` imports `C.ls`, `C.ls` resolves relative to `B.ls`'s directory.
- `module` (no dot) — resolved relative to the current working directory / project root.
- Paths are normalized via `realpath()` to prevent redundant compilation when the same file is imported through different relative paths.

### Module Structure

Each Lambda Script file is a module that can export public declarations — variables, functions, procedures, type aliases, and object types:

```lambda
// math_utils.ls

// Public values
pub PI = 3.14159
pub E = 2.71828

// Public functions
pub fn square(x: float) => x * x
pub fn cube(x: float) => x * x * x

// Public type alias
pub type Angle = float

// Public object type with methods
pub type Vec2 {
    x: float = 0.0, y: float = 0.0;
    fn len() => math.sqrt(x**2 + y**2)
    fn scale(f) => {Vec2 x: x*f, y: y*f}
}

// Public with error destructuring
pub config^err = input("config.json", 'json')

// Private (not exported)
let v = 123
fn helper(x: float) => x + 1
type Internal = {a: int, b: int}
```

### Using Imported Modules

```lambda
// main.ls
import .math_utils

// Imported values and functions are available directly
let area = PI * square(radius)

// Imported types can be used for annotations, construction, and type checks
let angle: Angle = 1.57
let v = {Vec2 x: 3.0, y: 4.0}
v.len()          // 5.0
v is Vec2        // true

// Error variables are also imported
if (err != null) print("config failed")
```

### Export Visibility

| Declaration | Visibility |
|-------------|------------|
| `pub x = ...` | Public (exported) |
| `pub fn f()` / `pub pn p()` | Public function/procedure |
| `pub type T = ...` | Public type alias |
| `pub type T { ... }` | Public object type |
| `pub x^err = ...` | Public value + error variable |
| `let x = ...` | Private (module-local) |
| `fn f()` / `pn p()` | Private function/procedure |
| `type T = ...` / `type T { ... }` | Private type |

### Built-in Module Imports

Lambda provides built-in modules (`math`, `io`) for mathematical functions and file system operations. These modules support three import styles:

```lambda
// 1. No import — use full module prefix (default, always available)
math.sqrt(16)         // 4
math.pi               // 3.1415926536
io.copy(@./a, @./b)

// 2. Global import — all functions available without prefix
import math;
sqrt(16)              // 4
pi                    // 3.1415926536
sin(0)                // 0

// 3. Aliased import — use a custom prefix
import m:math;
m.sqrt(16)            // 4
m.pi                  // 3.1415926536
m.sin(0)              // 0
```

| Style | Syntax | Usage | Best For |
|-------|--------|-------|----------|
| No import | *(none)* | `math.sqrt(x)` | Clarity, avoiding name conflicts |
| Global import | `import math;` | `sqrt(x)` | Math-heavy scripts, brevity |
| Aliased import | `import m:math;` | `m.sqrt(x)` | Short prefix, avoiding conflicts |

---

## Error Handling

Lambda uses an **error-as-return-value** paradigm — no `try`/`throw`/`catch` exceptions. Functions declare error return types with `T^E` syntax, raise errors with the `raise` keyword, and callers must explicitly handle errors using the `^` propagation operator or `let a^err` destructuring. Ignoring an error is a **compile-time error**.

```lambda
// Function that may fail
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a / b
}

// Propagate error with ?
let result = divide(10, x)^

// Or destructure to handle locally
let result^err = divide(10, x)
if (err != null) {
    print("error: " ++ err.message)
}
```

> **Full documentation**: See **[Lambda_Error_Handling.md](Lambda_Error_Handling.md)** for the complete guide — error types, `raise`, `^` operator, destructuring, enforcement rules, error codes, and examples.

---

## Examples

### Basic Data Processing

```lambda
// Read and process JSON data
let data = input("sales.json", 'json');

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

print(format(report, 'json'));
```

### Document Processing

```lambda
// Parse Markdown document
let doc = input("article.md", 'markdown');

// Query for all headings using type-based search
let headings = doc?(h1 | h2) | ~.content;

// Generate table of contents
let toc = <div class: "toc";
    <h2; "Table of Contents">
    <ul;
        for (heading in headings) <li; <a href: "#" ++ heading; heading>>
    >
>;

print(format(toc, 'html'));
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
        input(.config.json, 'json')
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
3. **Query Expressions**: jQuery-style search with `?` (descendants), `.?` (self-inclusive), and `[T]` (child-level)
4. **Pattern Matching**: Type-based pattern matching with `is`
5. **Document Processing**: Built-in support for markup and data formats

