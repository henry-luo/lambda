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

Lambda Script is a **general-purpose, cross-platform, pure functional scripting language** designed for data processing and document presentation. It is built from scratch in C/C++: a script starts on an AST interpreter and hot functions are compiled to native code through the MIR (Medium Internal Representation) JIT, over a garbage-collected value heap.

> **Alpha status.** The language is usable but still evolving. Features that the formal specification rules but the current build does not yet implement are marked **Not yet implemented** where they appear in these documents; the authoritative list is Appendix A of [Lambda_Formal_Semantics.md](Lambda_Formal_Semantics.md).

### Key Features

- **Pure functional core**: immutable data (arrays, maps, elements), first-class functions and types; `pn` procedures for controlled side effects
- **Two execution tiers**: an AST interpreter runs a script immediately and the MIR JIT compiles hot functions to native code (D8.1.1)
- **Cross-platform**: macOS, Linux and Windows with consistent behavior
- **Rich type system**: static checking with inference; union, occurrence and element types, string patterns, constrained types, nominal object types — and every type doubles as a schema
- **Document processing**: some 30 input formats and 20 output formats through one Mark data model, plus the Radiant layout and rendering engine (see [Lambda_Doc_Pipeline.md](Lambda_Doc_Pipeline.md))

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

The Lambda documentation is organized into focused documents. New to Lambda? Start with the **[Tutorial](tutorial/README.md)**: ten chapters from installation to a reactive app, every example checked against the current build. Read the language reference in the order of its table for a complete tour; the other tables are references.

### Language Reference

| Document | Description |
|----------|-------------|
| **[Lambda_Syntax.md](Lambda_Syntax.md)** | **Syntax Fundamentals** — Comments, statements and line continuation, identifiers and reserved words, strings, symbols, namespaces |
| **[Lambda_Data.md](Lambda_Data.md)** | **Literals and Collections** — Primitive types, path literals and references, arrays, maps, elements, ranges, data composition, document updates |
| **[Lambda_Type.md](Lambda_Type.md)** | **Type System** — First-class types, type hierarchy, union/occurrence/element types, function types, object types, constrained types, string patterns |
| **[Lambda_Expr_Stam.md](Lambda_Expr_Stam.md)** | **Expressions and Statements** — Arithmetic and vector arithmetic, comparisons, pipes (`\|>` `\|:` `that`), queries (`?` `.?` `[T]`), `if`/`for`/`match`, operators and precedence |
| **[Lambda_Func.md](Lambda_Func.md)** | **Functions** — `fn` and `pn` declarations, parameters, closures, higher-order and colour-polymorphic functions, method-style calls |
| **[Lambda_Procedural.md](Lambda_Procedural.md)** | **Procedural Programming** — `var`, assignment, value semantics, `while`, `return`, file output, the `io` module, `main()`, concurrency |
| **[Lambda_Error_Handling.md](Lambda_Error_Handling.md)** | **Error Handling** — Error values, `raise`, `T^E` return types, postfix `^` propagation, the `^ { }` handler, compile-time enforcement, error codes |
| **[Lambda_String_Pattern.md](Lambda_String_Pattern.md)** | **String Patterns** — The pattern language inside `\(…)`: character classes, ranges, quantifiers, negation, and pattern-aware `find`/`replace`/`split` |
| **[Lambda_Modules.md](Lambda_Modules.md)** | **Modules and Imports** — Import forms and resolution, `pub` exports, built-in and package modules, JavaScript modules |
| **[Lambda_Concurrency.md](Lambda_Concurrency.md)** | **Concurrency** — Tasks with `start`/`wait`, mailboxes, `select`, timeouts, cancellation, structured scope, JavaScript Promises |
| **[Lambda_Document_Updates.md](Lambda_Document_Updates.md)** | **Document Updates** — References and the force step `#`, node identity, `put`/`del`, transactions with `open`/`commit`/`rollback`, `temp.` documents |
| **[Lambda_Cheatsheet.md](Lambda_Cheatsheet.md)** | **Cheatsheet** — One-page syntax summary |

### Library and Tools

| Document | Description |
|----------|-------------|
| **[Lambda_Sys_Func.md](Lambda_Sys_Func.md)** | **System Functions** — Every built-in function: type, math, string, collection, date/time, I/O, concurrency |
| **[Lambda_CLI.md](Lambda_CLI.md)** | **CLI Reference** — Commands, flags, environment variables and usage of `lambda.exe` |
| **[Lambda_Packages.md](Lambda_Packages.md)** | **Packages** — The libraries written in Lambda that ship with the runtime: math, chart, graph, LaTeX, PDF, OpenAPI and the engine packages |
| **[Lambda_Validator.md](Lambda_Validator.md)** | **Validation** — Writing schemas with `type` declarations and validating files with `lambda validate` |

### Documents, Rendering and UI

| Document | Description |
|----------|-------------|
| **[Lambda_Doc_Pipeline.md](Lambda_Doc_Pipeline.md)** | **Document Pipeline** — The Mark data model, how input formats map onto it, the convert/validate/transform/render/view/edit workflows, and how Lambda compares with Pandoc, the XML stack, Typst and browsers |
| **[Markup_Formats_Support.md](Markup_Formats_Support.md)** | **Input and Output Formats** — Every supported markup and data format and the Mark tree each one produces |
| **[Doc_Schema.md](Doc_Schema.md)** | **Mark Doc Schema** — The shared element vocabulary for prose documents (Markdown, HTML, RST, wiki, …) |
| **[HTML_CSS_SVG_Support.md](HTML_CSS_SVG_Support.md)** | **HTML, CSS and SVG** — What Radiant supports: HTML elements, CSS selectors, properties and layout modes, SVG, fonts, images and output targets |
| **[Math_Support.md](Math_Support.md)** | **Math** — LaTeX and ASCII math input, the `math` rendering package, supported commands |
| **[Reactive_UI.md](Reactive_UI.md)** | **Reactive UI** — `view` and `edit` templates, `apply()` dispatch, template state and `on` event handlers |
| **[JS_DOM_Support.md](JS_DOM_Support.md)** | **LambdaJS** — The embedded JavaScript engine and browser DOM: conformance status, Node compatibility, benchmarks |

### Normative Specifications

| Document | Description |
|----------|-------------|
| **[Lambda_Formal_Semantics.md](Lambda_Formal_Semantics.md)** | **Formal Semantics** — `S`-numbered rulings: value domain, truthiness, numerics, equality, ordering, absence and errors, mutability, operators, types, functions, concurrency, syntax. The authority when documentation or implementation disagree; Appendix A lists what is not yet implemented |
| **[Lambda_Formal_Design.md](Lambda_Formal_Design.md)** | **Formal Design** — `D`-numbered rulings on the runtime's architecture, data representation, memory, stacks, functions, modules and compilation pipeline |
| **[Doc_Convention.md](Doc_Convention.md)** | **Documentation Convention** — Document tiers, authority order, code-fence markers and the doc-example gate |

### Developer Documentation

| Document | Description |
|----------|-------------|
| **[Developer_Guide.md](dev/Developer_Guide.md)** | **Developer Guide** — Build from source, dependencies, testing, grammar, MIR JIT |
| **[lambda/LR_00_Overview.md](dev/lambda/LR_00_Overview.md)** | **Lambda Core Runtime** — Compilation pipeline, value & type model, MIR-Direct transpiler and JIT, memory & GC, builtins |
| **[radiant/RAD_00_Overview.md](dev/radiant/RAD_00_Overview.md)** | **Radiant Engine** — CSS resolution, layout, rendering, events, editing, the application shell |
| **[js/JS_00_Overview.md](dev/js/JS_00_Overview.md)** | **LambdaJS Runtime** — The JavaScript engine's pipeline, value model, runtime, DOM and Node compatibility |

### Quick Reference

#### Data Types (see [Lambda_Data.md](Lambda_Data.md))

| Type      | Description                      | Example                       |
| --------- | -------------------------------- | ----------------------------- |
| `int`     | Integer, float64 safe range ±(2⁵³−1) | `42`, `-123`              |
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
| `object`  | Nominally-typed map with methods | `<Point x: 1, y: 2>`          |
| `element` | Markup element                   | `<div "content">`             |

`"..."` creates a `string`; `'...'` creates a `symbol`. They are different
types, so `'json' == "json"` is `false`; convert explicitly with `string(...)`
or `symbol(...)` when needed.

The empty string `""` is a real string value with length 0 and is falsy.
The empty symbol literal `''` is invalid; use `null` for absence.

#### Type System (see [Lambda_Type.md](Lambda_Type.md))

```lambda
// Type annotations
let x: int = 42
let items: string[] = ["a", "b"]

// Sized numeric type annotations
let a: i8 = 42i8
let b: u32 = 255u32
let c: f32 = 3.14f32

// Typed arrays are native storage; writing to one needs a pn
pn fill_native() {
    var arr: int[] = [1, 2, 3]       // native int array
    var data: float[] = [0.1, 0.2]   // native float array
    arr[0] = 9
}

// Union and optional types
type IntOrString = int | string    // either int or string
type MaybeInt = int?               // nullable: int | null

// Type declarations
type User = {name: string, age: int}
type HttpMethod = "GET" | "POST" | "PUT" | "DELETE"

// Object types (nominally-typed maps with methods)
type Point {
    x: float, y: float,
    fn distance(other: Point) => math.sqrt((x - other.x)**2 + (y - other.y)**2)
}
type Circle : Point { radius: float }    // Inheritance
let p = <Point x: 3.0, y: 4.0>           // Object literal
p.distance(<Point x: 0.0, y: 0.0>)       // Method call
p is Point                                // true (nominal)
```

#### Expressions (see [Lambda_Expr_Stam.md](Lambda_Expr_Stam.md))

| Kind | Expression | Result / meaning |
|---|---|---|
| Pipe | `[1, 2, 3] \|> ~ * 2` | `[2, 4, 6]` |
| Filter | `users \|> ~.name \|: len(~) > 3` | transform, then keep the names longer than 3 |
| Proviso | `x that ~ > 0` | `x` if the test holds, else `null` |
| Pipe/filter in an array | `[1, [2, 3] \|> ~, 4, 5]` | `[1, [2, 3], 4, 5]` — an array result is one item |
| | `[0, *(items \|: ~ > 3), 9]` | `*` splices the filtered items |
| Query — recursive | `html?<img>` | all `<img>` at any depth |
| | `html?<div class: string>` | `<div>` with a class attribute |
| | `data?int` | all int values in the tree |
| | `el.?<div>` | self-inclusive query |
| Query — child-level | `el[element]` | direct child elements |
| | `el[string]` | attribute values + text children |
| | `html[body]?<p>` | child then recursive (given `type body = <body>`) |
| For expression | `(for (x in [1,2,3] where x > 1 order by x desc) x * 2)` | clauses: `let` / `where` / `group by` / `order by` / `limit` / `offset` |
| | `(for (x in sales group by x.region into g) {region: g.region, n: len(content(g))})` | each group `g` is a `<group>` element — keys become attributes, members children, and `content(g)` is the members |
| | `(for (o in orders, c in customers on o.cust_id == c.id) {id: o.id, name: c.name})` | equi-join; `c?` = left join, `c` is null on no match |
| If | `if (x > 0) "positive" else "negative"` | |
| | `if x > 0 { compute(x) } else "default"` | block form, expression `else` |

```lambda
// String patterns (see Lambda_String_Pattern.md)
type digits = \(d+)
type email = \(w+ "@" w+ "." a{2,6})
type ident = \symbol(a w*)       // \symbol(...) matches symbols, \(...) strings
"123" is digits                  // true (full-match)
"12x" is \(d+)                   // false — patterns work inline too
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
    var obj = {a: 1}
    obj.a = "hello"       // map field type change
}

// Typed array parameters for native array access
pn advance(pos: float[], vel: float[], dt: float) {
    for i in range(0, len(pos)) {
        pos[i] = pos[i] + vel[i] * dt   // native float ops
    }
}

// Closures capture read-only snapshots; use var params for inout mutation
pn bump_first(var xs: any[]) {
    xs[0] = 42
}

pn main() {
    var xs: any[] = [1, 2, 3]
    bump_first(xs)
    print(xs[0])   // 42
}
```

#### Concurrency (see [Lambda_Concurrency.md](Lambda_Concurrency.md))

`pn` concurrency is colorless — there are no `async`/`await` keywords — and structured: `start(target, args)` launches a child task that belongs to the enclosing block, `wait` collects its result, and `send`/`receive` exchange messages through bounded mailboxes (S13).

```lambda
pn worker() {
    let message = receive()^
    return "done: " ++ message
}

pn main() {
    let handle = start(worker)
    send(handle, "job")^
    wait(handle)^          // "done: job"
}
```

---

## Modules and Imports

Each script file is a module. Declarations marked `pub` are exported, and `import` brings them into another file — directly, or under an alias. The same statement imports the built-in `math` and `io` modules, shipped packages under `lambda.*`, and JavaScript modules.

```lambda
// shapes.ls
pub let PI = 3.14159
pub fn area(r: float) => PI * r * r
pub type Circle { r: float, fn diameter() => r * 2 }
```

```lambda no-run
// no-run: imports shapes.ls, shown above
import .shapes             // binds PI, area and Circle directly
import s: .shapes          // or: s.PI, s.area, s.Circle
import m: math             // a built-in module under an alias
import tex: lambda.doc.math.math   // a shipped package

area(1.0)                  // 3.14159
```

A relative import `.a.b` resolves beside the importing file; a bare `a` resolves in the current working directory; `lambda.*` paths resolve under `LAMBDA_HOME`. Import forms, resolution, instantiation, JavaScript modules and known issues are described in [Lambda_Modules.md](Lambda_Modules.md).

---

## Error Handling

Lambda uses an **error-as-return-value** paradigm — no `try`/`throw`/`catch` exceptions. Functions declare error return types with `T^E` syntax, raise errors with the `raise` keyword, and callers must explicitly handle errors with the `^` propagation operator or the `^ { … }` handler. Ignoring an error is a **compile-time error**.

```lambda
// Function that may fail
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a / b
}

// Propagate error with ^
let propagated = divide(10, x)^

// Or handle it locally — `^` is the error
pn main() {                     // print is a pn: only a pn may call it
    let handled = divide(10, x) ^ {
        print("error: " ++ ^.message)
        0
    }
}
```

> **Full documentation**: See **[Lambda_Error_Handling.md](Lambda_Error_Handling.md)** for the complete guide — error types, `raise`, `^` operator, the `^ { }` handler, enforcement rules, error codes, and examples.

---

## Examples

### Basic Data Processing

```lambda
// Read and process JSON data
let data = input("sales.json", 'json')^;

// Calculate total sales
let total = data.sales |> ~.amount |> sum;

// Filter high-value sales
let high_value = data.sales |: ~.amount > 1000;

// Summarize by region — each group `g` is a <group> element
// (grouping key becomes an attribute, members become children)
let by_region = for (s in data.sales group by s.region into g)
    {region: g.region, total: sum(content(g) |> ~.amount), count: len(content(g))};

// Generate report
let report = {
    total_sales: total,
    high_value_count: len(high_value),
    by_region: by_region,
    average: total / len(data.sales),
    timestamp: datetime()
};

format(report, 'json')      // the script's result is its output
```

### Document Processing

```lambda
// Parse Markdown document
let doc = input("article.md", 'markdown')^;

// Query for all headings using type-based search
let headings = doc?(h1 | h2) |> ~.content;

// Generate table of contents
let toc = <div class: "toc",
    <h2 "Table of Contents">
    <ul
        for (heading in headings) <li <a href: "#" ++ heading, heading>>
    >
>;

format(toc, 'html')         // the script's result is its output
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

{factorials: factorials, fibonacci: fibs}   // the script's result is its output
```

### Procedural Script with Main

```lambda
// script.ls - Run with: lambda.exe run script.ls

pn main() {
    print("Starting processing...")

    // Load configuration (a file name with an extension is one quoted step)
    let config = if exists(\.'config.json') {
        input(\.'config.json', 'json')^
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
    output({processed: count, time: now()}, "./output/summary.json")^

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

1. **Two tiers**: an AST interpreter starts a script immediately; the MIR JIT compiles hot functions to native code
2. **Garbage collection**: a non-moving mark-and-sweep collector with precise rooting reclaims values; documents loaded by a parser live in an arena released as a whole
3. **Copy-on-write**: values are copied observably, but storage is shared until something changes
4. **Native lanes**: typed arrays and sized numbers run unboxed

### Expressiveness

Concise syntax for complex operations:

1. **Collection Comprehensions**: Powerful for-expressions for data processing
2. **Pipe Expressions**: Fluent data transformation pipelines
3. **Query Expressions**: jQuery-style search with `?` (descendants), `.?` (self-inclusive), and `[T]` (child-level)
4. **Pattern Matching**: Type-based pattern matching with `is`
5. **Document Processing**: Built-in support for markup and data formats
