# Lambda Procedural Programming

This document covers Lambda's procedural programming features — mutable state, imperative control flow, and side effects — all available exclusively inside `pn` (procedural) functions.

> **Related Documentation**:
> - [Lambda Expressions and Statements](Lambda_Expr_Stam.md) — Expressions, control flow, operators
> - [Lambda Functions](Lambda_Func.md) — Function definitions (`fn` and `pn`)
> - [Lambda Reference](Lambda_Reference.md) — Language overview

---

## Table of Contents

1. [Overview](#overview)
2. [Variable Declaration (`var`)](#variable-declaration-var)
3. [Assignment Statement](#assignment-statement)
4. [While Loop](#while-loop)
5. [Break and Continue](#break-and-continue)
6. [Early Return](#early-return)
7. [File Output Operators](#file-output-operators)
8. [I/O Module](#io-module)
9. [Procedural Functions (`pn`)](#procedural-functions-pn)
10. [Procedural vs Functional](#procedural-vs-functional)

---

## Overview

Lambda is a pure functional language by default. The `pn` keyword introduces **procedural functions** that allow mutable state and imperative control flow:

```lambda
pn counter() {
    var x = 0
    while (x < 5) {
        x = x + 1
    }
    x   // Returns 5
}
```

Procedural-only features:

| Feature | Syntax | Description |
|---------|--------|-------------|
| Mutable variables | `var x = 0` | Declare mutable bindings |
| Assignment | `x = value` | Reassign variables, array/map elements |
| While loop | `while (cond) { ... }` | Conditional looping |
| Break/Continue | `break`, `continue` | Loop control |
| Early return | `return expr` | Exit function early |
| File output | `\|>`, `\|>>`, `output()` | Write data to files |

### Running Procedural Scripts

```bash
./lambda.exe run script.ls    # Executes main() procedure
```

The `main()` procedure serves as the entry point for procedural scripts.

---

## Variable Declaration (`var`)

`var` declares a mutable variable. Unlike `let` (which is immutable), `var` bindings can be reassigned.

```lambda
pn example() {
    var x = 0
    x = x + 1      // OK: var is mutable
    
    let y = 10
    y = 20          // ERROR E211: cannot reassign immutable binding
}
```

### Type Widening

`var` variables without a type annotation support **type widening** — the variable's storage automatically adapts when assigned a value of a different type:

```lambda
pn example() {
    var x = 42       // int
    x = 3.14         // OK → float (type widened)
    x = "hello"      // OK → string (type widened)

    var y: int = 42  // annotated type
    y = "hello"      // ERROR E201: type mismatch
}
```

Variables with explicit type annotations (`var x: int`) enforce the declared type and reject mismatched assignments.

---

## Assignment Statement

Assignment (`=`) is only available inside `pn` functions. The left-hand side can be a variable, array element, map field, or element attribute/child.

### Immutability Rules

- **`let` bindings** are immutable — reassignment produces error E211
- **`pn` parameters** are mutable — reassignment is allowed (useful for counters, accumulators, etc.)
- **`fn` parameters** are immutable — reassignment produces error E211
- **`var` variables** are mutable — reassignment is allowed

```lambda
pn countdown(n) {
    while (n > 0) {   // OK: pn parameters are mutable
        n = n - 1
    }
}

pn example() {
    let x = 42
    x = 10           // ERROR E211: cannot reassign immutable binding
}

fn foo(x) {
    x + 1            // fn parameters are immutable (no assignment in fn)
}
```

### Variable Assignment

```lambda
pn example() {
    var x = 42
    x = 3.14       // OK: type widening (int → float)
    x = "hello"    // OK: type widening (float → string)

    var y: int = 10
    y = 20         // OK: same type
    y = "oops"     // ERROR E201: type mismatch (int expected)
}
```

### Array Element Assignment

```lambda
pn example() {
    let arr = [1, 2, 3]    // typed as int array
    arr[1] = 99            // OK: same type
    arr[0] = 3.14          // OK: auto-converts array to generic
    arr[-1] = "hello"      // OK: negative indexing supported
}
```

When a value of a different type is assigned to a typed array (e.g., float into an int array), the array is automatically converted to a generic array that can hold any type.

### Map Field Assignment

```lambda
pn example() {
    let obj = {name: "Alice", age: 30}
    obj.age = 31           // OK: same type
    obj.age = "thirty"     // OK: field type changes, shape rebuilt
    obj.name = null        // OK: any type transition
}
```

Map field assignment automatically rebuilds the map's shape metadata when the value type changes, ensuring structural consistency.

### Element Mutation

```lambda
pn example() {
    let elem = <div class: "main"; "Hello">
    elem.class = "updated"       // attribute assignment
    elem[0] = "Goodbye"          // child assignment by index
}
```

Elements support both attribute mutation (via dot notation) and child mutation (via index).

### Assignment Target Summary

| Target | Syntax | Behavior on Type Change |
|--------|--------|------------------------|
| Variable | `x = val` | Type widens (unannotated `var`) or error (annotated) |
| Array element | `arr[i] = val` | Array auto-converts from typed to generic |
| Map field | `obj.key = val` | Shape metadata auto-rebuilt |
| Element attr | `elem.attr = val` | Attribute updated in shape |
| Element child | `elem[i] = val` | Child replaced at index |

---

## While Loop

The `while` loop repeats a block while a condition is truthy:

```lambda
pn factorial(n: int) {
    var result = 1
    var i = 1
    while (i <= n) {
        result = result * i
        i = i + 1
    }
    result
}
```

While loops can be combined with `break` and `continue` for more complex control flow.

---

## Break and Continue

`break` exits the innermost loop immediately. `continue` skips to the next iteration.

```lambda
pn find_first_negative(nums: int[]) {
    var i = 0
    var result = null
    while (i < len(nums)) {
        if (nums[i] >= 0) {
            i = i + 1
            continue        // skip non-negative
        }
        result = nums[i]
        break               // found it, exit loop
    }
    result
}
```

```lambda
pn process_items(items) {
    var i = 0
    while (i < len(items)) {
        if (items[i] == null) {
            i = i + 1
            continue        // skip nulls
        }
        if (items[i] == 'stop') break   // stop sentinel
        process(items[i])
        i = i + 1
    }
}
```

---

## Early Return

`return` exits the function immediately with a value:

```lambda
pn find_first_even(nums: int[]) {
    var i = 0
    while (i < len(nums)) {
        if (nums[i] % 2 == 0) {
            return nums[i]   // Early exit
        }
        i = i + 1
    }
    null   // Not found
}
```

Without `return`, the last expression in the function body is the return value:

```lambda
pn add_one(x: int) {
    x + 1    // Implicitly returned
}
```

---

## File Output Operators

The pipe output operators `|>` and `|>>` write data to files. They are only available in `pn` functions.

| Operator | Description | Mode |
|----------|-------------|------|
| `\|>` | Pipe to file | Write (truncate) |
| `\|>>` | Pipe append | Append |

```lambda
pn save_report(data) {
    // Write to file (creates or overwrites)
    data |> "./temp/report.json"

    // Append to log
    {event: "saved", time: now()} |>> "./temp/events.log"

    // Using output function for more control
    output(data, "./temp/backup.json", {atomic: true})
}
```

---

## I/O Module

Lambda provides a unified I/O system that handles both local files and remote URLs transparently.

### Pure I/O Functions

Available anywhere (in both `fn` and `pn`):

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

## Procedural Functions (`pn`)

The `pn` keyword declares a **procedural function** — a function that can use mutable variables, assignment, loops, early return, and side effects.

### Declaration Syntax

`pn` mirrors `fn` in declaration forms — block body and expression body — but enables procedural features inside the body.

```lambda
// Block body
pn greet(name: string) {
    var msg = "Hello, " ++ name
    print(msg)
}

// Expression body
pn double(x: int) => x * 2

// With return type annotation
pn factorial(n: int) int {
    var result = 1
    var i = 1
    while (i <= n) {
        result = result * i
        i = i + 1
    }
    result
}
```

`pn` functions support the same parameter features as `fn`: required, optional (`?`), default values, rest parameters, and type annotations. See [Lambda Functions](Lambda_Func.md) for full parameter documentation.

### The `main()` Entry Point

A procedural script requires a `pn main()` function as its entry point. Use the `run` command to execute it:

```bash
lambda run script.ls           # Execute script.ls via main() (MIR Direct JIT, default)
lambda run --c2mir script.ls   # Execute with C2MIR JIT compilation
```

**Example script** (`greet.ls`):

```lambda
pn greet(name: string) {
    print("Hello, " ++ name ++ "!")
}

pn main() {
    greet("Alice")
    greet("Bob")

    var sum = 0
    var i = 1
    while (i <= 10) {
        sum = sum + i
        i = i + 1
    }
    print("Sum 1..10 = " ++ string(sum))
}
```

```bash
$ lambda run greet.ls
Hello, Alice!
Hello, Bob!
Sum 1..10 = 55
```

### Functional vs Procedural Script Execution

| Mode | Command | Entry Point | Features |
|------|---------|-------------|----------|
| Functional | `lambda script.ls` | Top-level expressions evaluated | Pure `fn`, `let`, expressions |
| Procedural | `lambda run script.ls` | `pn main()` called | `pn`, `var`, assignment, loops, I/O |

A functional script evaluates top-level expressions and prints results. A procedural script calls `main()` and relies on explicit `print()` or file output for results.

### Calling Between `fn` and `pn`

`pn` functions can call `fn` functions and vice versa. A common pattern is to define pure logic in `fn` and orchestrate I/O in `pn`:

```lambda
fn transform(data) => data | ~ * 2

pn main() {
    let input = [1, 2, 3, 4, 5]
    let result = transform(input)
    print(result)                    // [2, 4, 6, 8, 10]
    result |> "./temp/output.json"   // write to file
}
```

> **Note**: `fn` functions cannot use procedural features (`var`, assignment, `while`, `return`, file output). Attempting to do so produces a compile error.

### Procedural Methods in Object Types

Object types can define `pn` methods that mutate the object's fields in-place. In contrast, `fn` methods are pure and return new values without modifying the original object.

```lambda
type Counter {
    count: int = 0;

    fn value() => count                    // Pure — reads field
    pn increment() { count = count + 1 }   // Mutates field in-place
    pn reset() { count = 0 }               // Mutates field in-place
}

let c = {Counter}
c.increment()       // c.count is now 1
c.increment()       // c.count is now 2
c.value()           // 2
c.reset()           // c.count is now 0
```

Inside `pn` methods, bare field names resolve to the object's fields (same as in `fn` methods). When a parameter name shadows a field name, use `~` to disambiguate:

```lambda
type Account {
    balance: float,
    name: string;

    pn deposit(amount: float) {
        balance = balance + amount       // unambiguous: balance is a field
    }

    pn rename(name: string) {
        ~.name = name                    // ~.name = field, name = parameter
    }

    fn describe() => name ++ ": " ++ string(balance)
}
```

**Name resolution in methods:**

1. **Parameters first** — if a name matches a parameter, it refers to the parameter
2. **Fields second** — if unmatched, look up the object's fields
3. **Outer scope** — if still unmatched, standard lexical scoping applies
4. **`~` always refers to the object** — `~.field` is explicit self-access, never ambiguous

For full object type documentation (inheritance, defaults, constraints, composition), see [Lambda Type System — Object Types](Lambda_Type.md#object-types).

---

## Procedural vs Functional

| Feature | `fn` (Functional) | `pn` (Procedural) |
|---------|-------------------|--------------------|
| Mutable variables | No | Yes (`var`) |
| Assignment | No | Yes (`x = value`) |
| While loops | No | Yes |
| Break/Continue | No | Yes |
| Early return | No | Yes (`return`) |
| File output | No | Yes (`\|>`, `\|>>`) |
| Side effects | No | Allowed |
| Expression body | Yes | Yes |
| Block body | Yes | Yes |
| Closures | Yes | Yes |

**When to use `pn`:**
- Algorithms that need mutable state (counters, accumulators)
- I/O operations (file writing, console output)
- Imperative algorithms with early exit
- Script entry points (`main()`)

**When to use `fn`:**
- Pure transformations and computations
- Data processing pipelines
- Functions that should be composable and testable
- The default choice — use `fn` unless you need mutability

---

This document covers Lambda's procedural features. For function definitions, see [Lambda Functions](Lambda_Func.md). For expressions and operators, see [Lambda Expressions](Lambda_Expr_Stam.md).
