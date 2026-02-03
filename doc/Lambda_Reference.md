# Lambda Script Language Reference

## Table of Contents

1. [Introduction](#introduction)
2. [Language Overview](#language-overview)
3. [Syntax and Grammar](#syntax-and-grammar)
4. [Data Types](#data-types)
5. [Literals](#literals)
6. [Variables and Declarations](#variables-and-declarations)
7. [Expressions](#expressions)
   - [Pipe Expressions](#pipe-expressions)
8. [Control Flow](#control-flow)
   - [For Expressions with Clauses](#extended-for-expression-clauses)
9. [Functions](#functions)
10. [Collections](#collections)
11. [Type System](#type-system)
12. [System Functions](#system-functions)
13. [Input/Output and Parsing](#inputoutput-and-parsing)
14. [Operators](#operators)
    - [Pipe and Filter Operators](#pipe-and-filter-operators)
15. [Modules and Imports](#modules-and-imports)
16. [Error Handling](#error-handling)
17. [Memory Management](#memory-management)
18. [Examples](#examples)

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

## Syntax and Grammar

### Comments

```lambda
// Single-line comment

/*
   Multi-line comment
   can span multiple lines
*/
```

### Basic Structure

Lambda Script files contain expressions and statements:

```lambda
// Expressions (return values)
42
"hello world"
[1, 2, 3]

// Statements (declarations and control flow)
let x = 10;
if (x > 5) {
    print("x is greater than 5")
}
```

### Whitespace and Line Breaks

- Whitespace is generally ignored except in strings
- Line breaks can separate statements
- Semicolons (`;`) are used to terminate statements but are often optional

---

## Data Types

Lambda Script has a rich type system with both primitive and composite types:

### Primitive Types

| Type       | Description                 | Example           |
| ---------- | --------------------------- | ----------------- |
| `null`     | Null/void value             | `null`            |
| `bool`     | Boolean values              | `true`, `false`   |
| `int`      | 56-bit signed integers      | `42`, `-123`      |
| `float`    | 64-bit floating point       | `3.14`, `1.5e-10` |
| `decimal`  | Arbitrary precision decimal | `123.456n`        |
| `string`   | UTF-8 text strings          | `"hello"`         |
| `symbol`   | Interned identifiers        | `'symbol`         |
| `binary`   | Binary data                 | `b'\xDEADBEEF'`   |
| `datetime` | Date and time values        | `t'2025-01-01'`   |

### Composite Types

| Type | Description | Example |
|------|-------------|---------|
| `list` | Immutable ordered sequences | `(1, 2, 3)` |
| `array` | Mutable ordered collections | `[1, 2, 3]` |
| `map` | Key-value mappings | `{key: "value"}` |
| `element` | Structured markup elements | `<tag attr: value; content>` |
| `range` | Numeric ranges | `1 to 10` |
| `function` | Function values | `(x) => x + 1` |
| `type` | Type descriptors | `int`, `string` |

### Special Types

- `any` - Any type (top type)
- `error` - Error values
- `number` - Numeric union type (int | float)

---

## Literals

### Numeric Literals

```lambda
// Integers
42
-123
0

// Floats
3.14
-2.5
1.5e-10
1e6
inf
nan
-inf

// Decimals (arbitrary precision)
123.456n
-789.012N
```

### String Literals

```lambda
// Basic strings
"hello world"
"multiline strings
can span multiple lines"

// Escape sequences
"line 1\nline 2"
"tab\there"
"quote: \"hello\""
```

### Symbol Literals

```lambda
'identifier
'symbol-name
'CamelCase
```

### Binary Literals

```lambda
// Hexadecimal
b'\xDEADBEEF'
b'\xA0FE af0d'

// Base64
b'\64A0FE'
b'\64A0FE gh8='
b'\64A0FE gh=='
```

### DateTime Literals

```lambda
// Dates
t'2025'           // Year only
t'2025-06'        // Year-month
t'2025-04-26'     // Full date
t'-1000-12-25'    // Historical dates

// Times
t'10:30'          // Hour:minute
t'10:30:45'       // Hour:minute:second
t'10:30:45.123'   // With milliseconds
t'10:30+08'       // With timezone

// Date-time combinations
t'2025-05-01 10:30'
t'2025-05-01T14:30:00Z'
```

### Boolean and Null Literals

```lambda
true
false
null
```

---

## Variables and Declarations

### Let Expressions

Let expressions introduce local bindings:

```lambda
// Single binding
(let x = 42, x + 1)  // Returns 43

// Multiple bindings
(let a = 1, let b = 2, a + b)  // Returns 3

// Complex expressions
(let data = [1, 2, 3], 
 let doubled = (for (x in data) x * 2),
 doubled)
```

### Let Statements

Let statements declare variables in the current scope:

```lambda
let x = 42;
let y = 3.14, z = true;
let name = "Alice", age = 30;
```

### Public Declarations

Public declarations export variables:

```lambda
pub PI = 3.14159;
pub greeting = "Hello, World!";
```

### Type Annotations

Variables can have explicit type annotations:

```lambda
let x: int = 42;
let name: string = "Alice";
let items: [int] = [1, 2, 3];
```

---

## Expressions

### Primary Expressions

```lambda
// Literals
42
"hello"
true

// Variables
x
myVariable

// Parenthesized expressions
(x + y)

// Collection access
arr[0]
map.key
obj["field"]
```

### Arithmetic Expressions

```lambda
// Basic arithmetic
5 + 3     // Addition
5 - 3     // Subtraction
5 * 3     // Multiplication
10 / 3    // Division
10 div 3  // Integer division
17 % 5    // Modulo
2 ^ 3     // Exponentiation

// Unary operators
-x        // Negation
+x        // Positive (identity)
```

### Vector Arithmetic (Implicit Element-wise Operations)

Lambda supports NumPy-style element-wise arithmetic on vectors. When arithmetic operators are applied to arrays, they operate element-by-element.

#### Scalar-Vector Operations

```lambda
1 + [2, 3, 4]              // [3, 4, 5]
10 - [1, 2, 3]             // [9, 8, 7]
3 * [1, 2, 3]              // [3, 6, 9]
12 / [2, 3, 4]             // [6.0, 4.0, 3.0]
2 ^ [1, 2, 3]              // [2, 4, 8]
```

#### Vector-Vector Operations

```lambda
[1, 2, 3] + [4, 5, 6]      // [5, 7, 9]
[10, 20, 30] - [1, 2, 3]   // [9, 18, 27]
[2, 3, 4] * [1, 2, 3]      // [2, 6, 12]
[12, 15, 18] / [3, 5, 6]   // [4.0, 3.0, 3.0]
```

#### Broadcasting Rules

- **Single-element broadcast**: `[5] + [1, 2, 3]` → `[6, 7, 8]`
- **Size mismatch**: `[1, 2] + [3, 4, 5]` → Error

See [Lambda_Type_Vector.md](../vibe/Lambda_Type_Vector.md) for detailed vector operation documentation.

### Comparison Expressions

```lambda
// Equality
5 == 5    // Equal
5 != 3    // Not equal

// Relational
3 < 5     // Less than
5 > 3     // Greater than
3 <= 5    // Less than or equal
5 >= 3    // Greater than or equal
```

### Logical Expressions

```lambda
// Boolean operators
true and false   // Logical AND
true or false    // Logical OR
not true         // Logical NOT

// Short-circuit evaluation
(x > 0) and (y / x > 2)
```

### Truthy and Falsy Values

Lambda Script uses truthy/falsy semantics in boolean contexts (`and`, `or`, `if`, `not`). A value is considered **falsy** if it is one of:

| Falsy Value | Description |
|-------------|-------------|
| `null` | Null value |
| `false` | Boolean false |

All other values are **truthy**, including:

| Truthy Value | Example |
|--------------|--------|
| `true` | Boolean true |
| Non-zero numbers | `1`, `-1`, `0.5`, `42` |
| Zero | `0`, `0.0` (Note: zero IS truthy in Lambda) |
| Non-empty strings | `"hello"`, `" "` |
| Empty string | `""` (Note: empty string IS truthy) |
| Collections | `[]`, `{}`, `()` (all truthy, even if empty) |
| Functions | Any function value |

**Important**: Unlike many languages, Lambda considers `0`, `""`, and empty collections as truthy. Only `null` and `false` are falsy.

```lambda
// Truthy examples
if (0) "yes" else "no"           // "yes" - 0 is truthy
if ("") "yes" else "no"          // "yes" - empty string is truthy
if ([]) "yes" else "no"          // "yes" - empty array is truthy

// Falsy examples
if (null) "yes" else "no"        // "no" - null is falsy
if (false) "yes" else "no"       // "no" - false is falsy

// Short-circuit with truthy values
let x = null or "default"        // "default" - null is falsy, returns right
let y = "value" or "default"     // "value" - string is truthy, returns left
let z = "value" and "other"      // "other" - both truthy, returns right
```

### Null Comparisons

Null can be compared with any type using `==` and `!=`:

```lambda
null == null        // true
null == 42          // false (not an error)
"hello" != null     // true
let x = get_value()
if (x == null) "missing" else x  // Idiomatic null check
```

### Member Access and Null Safety

Lambda's `.` operator for member access has **built-in null safety**, similar to the optional chaining operator (`?.`) in JavaScript/TypeScript. When the object is `null`, accessing any member returns `null` instead of throwing an error.

```lambda
// Direct null member access
let x = null
x.name           // null (not an error)
x.a.b.c          // null (null propagates through the chain)

// Null in nested structures
let user = {name: "Alice", address: null}
user.name              // "Alice"
user.address           // null
user.address.city      // null (safe - no error)
user.address.city.zip  // null (continues to propagate)
```

#### Null Propagation Chain

Null propagates automatically through chained member access:

```lambda
// Given potentially null objects at any level
let result = company.department.manager.name

// Equivalent to this verbose null-checking in other languages:
// if (company == null) null
// else if (company.department == null) null
// else if (company.department.manager == null) null
// else company.department.manager.name
```

#### Method Calls on Null

When the receiver of a method call is null, the call returns null without invoking the method:

```lambda
let items = null
items.len()              // 0 (len of null is 0)
items.reverse()          // null

let arr = [1, 2, 3]
arr.len()                // 3
arr.reverse()            // [3, 2, 1]
```

#### Index Access on Null

Index access on null also returns null:

```lambda
let data = null
data[0]        // null
data["key"]    // null

let arr = [1, 2, 3]
arr[0]         // 1
arr[10]        // null (out of bounds also returns null)
```

#### Comparison with JavaScript

| Syntax | JavaScript | Lambda |
|--------|------------|--------|
| Regular access | `obj.field` (throws if null) | `obj.field` (returns null if obj is null) |
| Optional chaining | `obj?.field` | `obj.field` (same behavior) |
| Method call | `obj.method()` (throws if null) | `obj.method()` (returns null if obj is null) |

**Key Insight**: Lambda's `.` operator behaves like JavaScript's `?.` by default. This design choice eliminates null reference errors and enables clean, functional-style data processing.

#### Practical Patterns

```lambda
// Safe nested access
let config = input("config.json", 'json')
let port = config.server.port      // null if any level is missing

// Conditional with null check
let city = if (user.address != null) user.address.city else "Unknown"

// Using or for defaults
let name = user.profile.display_name or user.name or "Anonymous"
```

### Type Expressions

```lambda
// Type queries
type(42)          // Returns 'int'
type("hello")     // Returns 'string'

// Type checking
42 is int         // Returns true
"hello" is string // Returns true
```

### Pipe Expressions

The **pipe operator** (`|`) enables fluent, left-to-right data transformation pipelines. Lambda's pipe is **set-oriented** — it automatically maps over collections, similar to how arithmetic operators broadcast over arrays.

#### Basic Syntax

```lambda
<collection> | <expression-with-~>
```

- **`|`** — Set-oriented pipe operator
- **`~`** — Current item reference (the element being iterated)

#### Auto-Mapping Over Collections

When the left side is a collection, `~` binds to each item:

```lambda
// Double each number
[1, 2, 3] | ~ * 2
// Result: [2, 4, 6]

// Extract field from each item
users | ~.name
// Result: ["Alice", "Bob", "Charlie"]

// Transform each item
["hello", "world"] | ~ ++ "!"
// Result: ["hello!", "world!"]
```

#### Scalar Pipe

When the left side is a scalar, `~` binds to the whole value:

```lambda
42 | ~ * 2
// Result: 84

"hello" | ~ ++ " world"
// Result: "hello world"
```

#### Chained Transformations

Pipes can be chained for multi-step transformations:

```lambda
[1, 2, 3, 4, 5]
    | ~ ^ 2           // square each: [1, 4, 9, 16, 25]
    | ~ + 1           // add 1: [2, 5, 10, 17, 26]
// Result: [2, 5, 10, 17, 26]
```

#### Key/Index Access with `~#`

The `~#` token provides access to the **key** (for maps) or **index** (for arrays/lists):

```lambda
// Arrays — ~# is index (0-based)
['a', 'b', 'c'] | {index: ~#, value: ~}
// [{index: 0, value: 'a'}, {index: 1, value: 'b'}, {index: 2, value: 'c'}]

// Maps — ~ is value, ~# is key
{a: 1, b: 2} | {key: ~#, val: ~}
// [{key: 'a', val: 1}, {key: 'b', val: 2}]
```

#### Aggregated Pipe (without `~`)

When `~` is **not** used, the pipe passes the **entire collection** as the first argument:

```lambda
// No ~ → pass whole collection to function
[3, 1, 4, 1, 5] | sum        // sum([3, 1, 4, 1, 5]) → 14
[3, 1, 4, 1, 5] | sort       // sort([...]) → [1, 1, 3, 4, 5]
[1, 2, 3, 4, 5] | take(3)    // take([...], 3) → [1, 2, 3]

// Chain of transformations ending with aggregation
[1, 2, 3, 4, 5]
    | ~ ^ 2            // map: [1, 4, 9, 16, 25]
    | sum              // aggregate: 55
```

#### Where Clause (Filtering)

The `where` keyword filters items, keeping only those where the condition is truthy:

```lambda
// Basic filtering
[1, 2, 3, 4, 5] where ~ > 3
// Result: [4, 5]

// Filter objects
users where ~.age >= 18
// Keep only adult users

// Chained with pipes
data | ~.name where len(~) > 3 | ~.upper()
// Extract names, keep those longer than 3 chars, uppercase
```

#### Combined Pipe and Where

```lambda
// Data processing pipeline
input("users.json", 'json').users
    | {name: ~.name, age: ~.age}     // project fields
    where ~.age >= 18                 // filter adults  
    | ~.name                          // extract names
// Result: ["Alice", "Bob", ...]

// Sum of squares greater than 10
[1, 2, 3, 4, 5] | ~ ^ 2 where ~ > 10 | sum
// [1, 4, 9, 16, 25] → [16, 25] → 41
```

#### Behavior Summary

| Left Side | `~` Binds To | `~#` Binds To | Result |
|-----------|--------------|---------------|--------|
| `[a, b, c]` (array) | Each element | Index (0, 1, 2) | Array of transformed elements |
| `(a, b, c)` (list) | Each element | Index (0, 1, 2) | List of transformed elements |
| `1 to 10` (range) | Each number | Position (0-9) | Array of results |
| `{a: 1, b: 2}` (map) | Each value | Key ('a', 'b') | Collection of results |
| `42` (scalar) | The value itself | N/A | Single transformed value |

---

## Control Flow

### If Expressions

If expressions require both then and else branches:

```lambda
// Simple if expression
(if (x > 0) "positive" else "non-positive")

// Nested if expressions
(if (score >= 90) "A" 
 else if (score >= 80) "B" 
 else "C")

// If expressions in let bindings
(let x = 5, if (x > 3) "big" else "small")
```

### If Statements

If statements can have optional else clauses:

```lambda
if (x > 0) {
    print("x is positive")
}

if (temperature > 30) {
    print("It's hot!")
} else {
    print("It's not too hot")
}
```

### For Expressions

For expressions iterate over collections and return new collections:

```lambda
// Array iteration
(for (x in [1, 2, 3]) x * 2)  // [2, 4, 6]

// Range iteration
(for (i in 1 to 5) i * i)     // [1, 4, 9, 16, 25]

// Conditional iteration
(for (num in [1, 2, 3, 4, 5]) 
    if (num % 2 == 0) num else 0)

// String iteration
(for (item in ["a", "b", "c"]) item + "!")
```

#### Extended For-Expression Clauses

For expressions support additional clauses for filtering, sorting, and limiting results, inspired by SQL and XQuery's FLWOR expressions:

```
for (<bindings>
     [let <name> = <expr>, ...]
     [where <condition>]
     [order by <ordering>]
     [limit <n>]
     [offset <n>])
  <body-expr>
```

##### Where Clause

Filter items based on a condition:

```lambda
// Filter values greater than 2
for (x in [1, 2, 3, 4, 5] where x > 2) x
// Result: (3, 4, 5)

// Filter with complex condition
for (user in users where user.active and user.age >= 18) user.name
```

##### Let Clause

Introduce intermediate bindings for computed values:

```lambda
// Compute derived value
for (x in [1, 2, 3], let squared = x * x) squared + 1
// Result: (2, 5, 10)

// Multiple let bindings
for (x in [2, 3, 4], let sq = x * x, let cube = sq * x) [x, sq, cube]
// Result: ([2, 4, 8], [3, 9, 27], [4, 16, 64])

// Let with where (let is evaluated before where)
for (x in [1, 2, 3, 4, 5], let doubled = x * 2 where doubled > 4) doubled
// Result: (6, 8, 10)
```

##### Order By Clause

Sort results by one or more expressions:

```lambda
// Ascending order (default)
for (x in [3, 1, 4, 1, 5] order by x) x
// Result: (1, 1, 3, 4, 5)

// Descending order
for (x in [3, 1, 4, 1, 5] order by x desc) x
// Result: (5, 4, 3, 1, 1)

// Order by computed value
for (user in users order by user.lastName, user.firstName) user

// Order by field descending
for (p in products order by p.price desc) p.name
```

**Direction keywords:**

| Keyword | Meaning | Aliases |
|---------|---------|--------|
| `asc` | Ascending (A→Z, 0→9) | `ascending` |
| `desc` | Descending (Z→A, 9→0) | `descending` |

##### Limit and Offset Clauses

Control the number of results and pagination:

```lambda
// First 3 results
for (x in [1, 2, 3, 4, 5] limit 3) x
// Result: (1, 2, 3)

// Skip first 2 results
for (x in [1, 2, 3, 4, 5] offset 2) x
// Result: (3, 4, 5)

// Pagination: skip 2, take 3
for (x in [1, 2, 3, 4, 5, 6, 7] limit 3 offset 2) x
// Result: (3, 4, 5)

// Pagination with sorting
for (item in items order by item.date desc limit 10 offset 20) item
```

##### Combined Clauses

All clauses can be combined for powerful data processing:

```lambda
// Complete example: filter, compute, sort, paginate
for (x in [10, 5, 15, 20, 25, 3],
     let squared = x * x
     where x > 5
     order by squared desc
     limit 2
     offset 1)
  squared
// Filter x > 5: [10, 15, 20, 25]
// Compute squared: [100, 225, 400, 625]
// Sort desc: [625, 400, 225, 100]
// Offset 1, limit 2: [400, 225]
// Result: (400, 225)

// Real-world example: top customers
for (customer in customers,
     let total = sum(customer.orders | ~.amount)
     where customer.active
     order by total desc
     limit 10)
  {name: customer.name, total: total}
```

##### Clause Ordering

Clauses must appear in this order (all optional except bindings):

```
for (bindings, let... where... order by... limit... offset...) body
```

**Processing order:**
1. **for bindings** — Generate iteration tuples
2. **let** — Compute intermediate values
3. **where** — Filter tuples
4. **order by** — Sort results
5. **offset** — Skip initial results
6. **limit** — Cap result count
7. **body** — Evaluate and collect

### For Statements

For statements execute code for each iteration:

```lambda
for item in [1, 2, 3] {
    print(item)
}

for i in 1 to 10 {
    if (i % 2 == 0) {
        print(i, "is even")
    }
}

// Multiple loop variables
for x in [1, 2], y in [3, 4] {
    print(x, y)
}
```

---

## Functions

### Function Declarations

```lambda
// Named function statement
fn add(a: int, b: int) int {
    a + b
}

// Function expression statement  
fn multiply(x: int, y: int) => x * y

// Anonymous function expressions
(x: int) => x * 2
fn(x: int, y: int) => x + y

// Multi-statement function body
fn process(data: [int]) [int] {
    let filtered = (for (x in data) if (x > 0) x else 0);
    let doubled = (for (x in filtered) x * 2);
    doubled
}
```

### Function Parameters

Lambda supports flexible parameter handling with optional parameters, default values, named arguments, and variadic parameters.

#### Required Parameters

Parameters with type annotations are required by default:

```lambda
fn greet(name: string) => "Hello, " + name
fn add(a: int, b: int) => a + b

add(5, 3)       // 8
add(5)          // Error: missing required parameter
```

#### Optional Parameters

Use `?` before the type annotation to make a parameter optional:

```lambda
fn greet(name: string, title?: string) => {
    if (title) title + " " + name
    else name
}

greet("Alice")              // "Alice"
greet("Alice", "Dr.")       // "Dr. Alice"
```

**Note**: `a?: T` means parameter `a` is optional and may be omitted. Inside the function, `a` will be `null` if not provided. This is different from `a: T?` which means the type is nullable but the parameter is still required.

#### Default Parameter Values

Parameters can have default values, making them optional:

```lambda
fn greet(name = "World") => "Hello, " + name + "!"
fn power(base: int, exp: int = 2) => base ^ exp

greet()             // "Hello, World!"
greet("Lambda")     // "Hello, Lambda!"
power(3)            // 9 (3^2)
power(2, 10)        // 1024 (2^10)
```

Default expressions are evaluated at call site (lazy evaluation) and can reference earlier parameters:

```lambda
fn make_rect(width: int, height = width) => {
    width: width, height: height
}

make_rect(10)       // {width: 10, height: 10}
make_rect(10, 20)   // {width: 10, height: 20}
```

#### Named Arguments

Arguments can be passed by name, allowing any order:

```lambda
fn create_user(name: string, age: int, active: bool = true) => {
    name: name, age: age, active: active
}

// All equivalent:
create_user("Alice", 30, true)
create_user(name: "Alice", age: 30, active: true)
create_user(age: 30, name: "Alice")  // Order independent

// Skip optional parameters
create_user("Bob", 25)               // active defaults to true
```

**Rules for named arguments**:
- Positional arguments must come before named arguments
- Named arguments can appear in any order
- Cannot provide the same argument both positionally and by name

#### Variadic Parameters

Use `...` to accept any number of additional arguments:

```lambda
fn sum_all(...) => sum(varg())
fn printf(fmt: string, ...) => format(fmt, varg())

sum_all(1, 2, 3, 4, 5)      // 15
sum_all()                    // 0 (empty list)
printf("%s is %d", "x", 42) // "x is 42"
```

Access variadic arguments with the `varg()` system function:

| Call | Returns |
|------|--------|
| `varg()` | List of all variadic arguments |
| `varg(n)` | The nth variadic argument (0-indexed) |

```lambda
fn first_or_default(default, ...) => {
    if (len(varg()) > 0) varg(0)
    else default
}

first_or_default(0, 1, 2, 3)   // 1
first_or_default(0)             // 0
```

#### Parameter Order

Parameters must be declared in this order:
```
required → optional (?) → defaults → variadic (...)
```

```lambda
// Valid
fn valid(req: int, opt?: int, def: int = 10, ...) => { ... }

// Invalid - optional before required
fn invalid(opt?: int, req: int) => { ... }  // Error
```

#### Parameter Count Mismatch Handling

| Situation | Behavior |
|-----------|----------|
| Missing required argument | Compile-time error |
| Missing optional argument | Filled with `null` |
| Missing default argument | Evaluates default expression |
| Extra arguments (no variadic) | Warning, arguments discarded |

### Function Calls

```lambda
// Function calls
add(5, 3)
greet("Alice")
calculate(2.5, 3.0, "add")

// Higher-order functions
let numbers = [1, 2, 3, 4, 5];
let doubled = (for (x in numbers) multiply(x, 2))
```

### Closures

A **closure** is a function that captures variables from its enclosing scope. The captured variables are preserved even after the outer function returns, allowing the inner function to access them later.

#### Basic Closure

```lambda
let multiplier = 3
let triple = (x) => x * multiplier
triple(5)  // 15
```

#### Factory Functions (Closures Returning Functions)

Closures are commonly used in factory functions that return customized functions:

```lambda
fn make_adder(base: int) {
    fn adder(y) => base + y  // captures 'base' from outer scope
    adder
}

let add10 = make_adder(10)
let add100 = make_adder(100)

add10(5)    // 15
add100(5)   // 105
```

Each call to `make_adder` creates a new closure with its own captured `base` value.

#### Capturing Multiple Variables

Closures can capture multiple variables from their enclosing scope:

```lambda
fn make_linear(slope: int, intercept: int) {
    fn eval(x) => slope * x + intercept  // captures both slope and intercept
    eval
}

let line = make_linear(2, 10)
line(5)   // 20 (2*5 + 10)
line(10)  // 30 (2*10 + 10)
```

#### Nested Closures

Closures can be nested multiple levels deep, with each level capturing from its parent:

```lambda
fn level1(a) {
    fn level2(b) {
        fn level3(c) {
            fn level4(d) => a + b + c + d  // captures a, b, c from outer scopes
            level4
        }
        level3
    }
    level2
}

level1(1)(2)(3)(4)  // 10
```

#### Capturing Let Variables

Closures can capture `let` bindings computed in the outer scope:

```lambda
fn make_counter(start: int) {
    let initial = start * 2    // computed value
    fn count(step) => initial + step  // captures 'initial'
    count
}

let counter = make_counter(5)
counter(3)  // 13 (5*2 + 3)
```

#### Closures with Conditionals

Closures can contain any expression, including conditionals:

```lambda
fn make_threshold_checker(threshold: int) {
    fn check(x) => if (x < threshold) -x else x
    check
}

let abs_zero = make_threshold_checker(0)
abs_zero(-5)  // 5
abs_zero(5)   // 5
```

#### Higher-Order Functions with Closures

Closures work seamlessly with higher-order functions:

```lambda
fn apply(f, x) => f(x)
fn compose(f, g) => (x) => g(f(x))

let add1 = (x) => x + 1
let double = (x) => x * 2

apply(add1, 5)           // 6
compose(add1, double)(5) // 12 (double(add1(5)) = double(6) = 12)
```

#### Closure Limitations

Current limitations (may be addressed in future versions):

- **Immutable captures only**: Closures capture values, not references. Modifying a captured variable in the outer scope after closure creation does not affect the closure.
- **No mutable state**: Closures cannot maintain mutable state between calls. Use procedural functions (`pn`) if mutable state is needed.

### Procedural Functions

Lambda also supports **procedural functions** using the `pn` keyword. Unlike pure functional `fn` functions, procedural functions allow mutable state, side effects, and imperative control flow.

```lambda
// Procedural function declaration
pn counter() {
    var x = 0          // Mutable variable
    while (x < 5) {
        x = x + 1      // Assignment
    }
    x                  // Returns 5
}
```

#### Key Differences from `fn`

| Feature | `fn` (Functional) | `pn` (Procedural) |
|---------|-------------------|-------------------|
| Mutable variables | No | Yes (`var`) |
| Assignment | No | Yes (`x = value`) |
| While loops | No | Yes (`while`) |
| Break/Continue | No | Yes (`break`, `continue`) |
| Early return | No | Yes (`return`) |
| File output | No | Yes (`output()`) |
| Side effects | Discouraged | Allowed |

#### Implicit Return Value

Like Rust, Ruby, and Scala, **the last expression in a procedural function becomes its return value**:

```lambda
pn add_one(x: int) {
    x + 1    // Last expression is returned
}

pn factorial(n: int) {
    var result = 1
    var i = 1
    while (i <= n) {
        result = result * i
        i = i + 1
    }
    result   // Last expression is returned
}

add_one(5)      // Returns 6
factorial(5)    // Returns 120
```

This design follows the principle that **everything is an expression** - even statement blocks evaluate to a value.

#### Early Return with `return`

Use `return` to exit early from a procedural function:

```lambda
pn find_first_even(nums: [int]) {
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

#### Procedural Control Flow

Procedural functions support imperative control structures:

```lambda
// While loop
pn countdown(n: int) {
    var x = n
    while (x > 0) {
        print(x)
        x = x - 1
    }
    "Done"
}

// Mutable variables
pn swap_values() {
    var a = 1
    var b = 2
    var temp = a
    a = b
    b = temp
    (a, b)   // Returns (2, 1)
}
```

#### Break and Continue

Use `break` to exit a loop early, and `continue` to skip to the next iteration:

```lambda
// Break - exit loop when condition met
pn find_first_negative(nums: [int]) {
    var i = 0
    var result = null
    while (i < len(nums)) {
        if (nums[i] < 0) {
            result = nums[i]
            break           // Exit the loop
        }
        i = i + 1
    }
    result
}

// Continue - skip even numbers
pn sum_odd_numbers(n: int) {
    var sum = 0
    var i = 0
    while (i < n) {
        i = i + 1
        if (i % 2 == 0) {
            continue        // Skip to next iteration
        }
        sum = sum + i
    }
    sum   // 1 + 3 + 5 + ... 
}
```

#### If Statements in Procedural Code

Procedural functions support C-style if/else statements (the else clause is optional):

```lambda
pn classify(x: int) {
    var result = ""
    if (x > 0) {
        result = "positive"
    } else {
        if (x < 0) {
            result = "negative"
        } else {
            result = "zero"
        }
    }
    result
}

// If without else
pn abs_value(x: int) {
    var result = x
    if (x < 0) {
        result = -x
    }
    result
}
```

#### Running Procedural Scripts

Use the `run` command to execute scripts with a `main()` procedure:

```bash
./lambda.exe run script.ls    # Executes main() procedure
```

The `main()` procedure serves as the entry point for procedural scripts.

---

## Collections

### Lists

Immutable ordered sequences (tuples):

```lambda
// List creation
(1, 2, 3)
("hello", "world")
(true, 42, "mixed")

// List access
let lst = (1, 2, 3);
lst[0]    // First element: 1
lst[1]    // Second element: 2
lst[-1]   // Last element: 3

// List operations
len(lst)  // Length: 3
```

### Arrays

Mutable ordered collections:

```lambda
// Array creation
[1, 2, 3]
["a", "b", "c"]
[true, false, null]

// Mixed-type arrays
[42, "hello", 3.14, true, null]

// Nested arrays
[[1, 2], [3, 4], [5, 6]]

// Array access
let arr = [10, 20, 30];
arr[0]     // 10
arr[1]     // 20
arr[-1]    // 30 (last element)

// Array operations
len(arr)   // 3
```

### Maps

Key-value mappings:

```lambda
// Map creation
{key: "value"}
{name: "Alice", age: 30, active: true}

// Mixed key types
{"string_key": 1, symbol_key: 2, 3: "number_key"}

// Nested maps
{
    user: {name: "Bob", email: "bob@example.com"},
    settings: {theme: "dark", notifications: true}
}

// Map access
let person = {name: "Charlie", age: 25};
person.name      // "Charlie"
person["name"]   // "Charlie" 
person.age       // 25

// Map operations
len(person)      // 2
```

### Elements

Structured markup elements with attributes and content:

```lambda
// Basic element
<tag>

// Element with attributes
<div id: "main", class: "container">

// Element with content
<p; "Hello, world!">

// Element with attributes and content
<div class: "header"; 
    "Page Title";
    <span; "Subtitle">
>

// Complex elements
<article title: "My Article", author: "John Doe";
    <h1; "Introduction">
    <p; "This is the first paragraph.">
    <p; "This is the second paragraph.">
>
```

### Ranges

Numeric sequences:

```lambda
// Basic ranges
1 to 10        // [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
0 to 5         // [0, 1, 2, 3, 4, 5]
-2 to 2        // [-2, -1, 0, 1, 2]

// Range operations
len(1 to 10)   // 10
(1 to 5)[2]    // 3

// Range in for loops
(for (i in 1 to 5) i * i)  // [1, 4, 9, 16, 25]
```

---

## Type System

### Basic Types

```lambda
// Type literals
null
bool
int
float
decimal
string
symbol
binary
datetime
```

### Collection Types

```lambda
// Array types
[int]              // Array of integers
[string]           // Array of strings
[[int]]            // Array of integer arrays

// List types
(int, string)      // List with int and string
(string, string, string)  // List of three strings

// Map types
{name: string, age: int}  // Map with specific fields

// Element types
<tag attr: string; content_type>
```

### Union Types

```lambda
// Union types (using |)
int | string       // Either int or string
int | float | string  // One of int, float, or string

// Nullable types
int | null         // Integer or null
string | null      // String or null
```

### Function Types

```lambda
// Function type syntax (for type declarations)
fn (int) int              // Function taking int, returning int
fn (int, int) int         // Function taking two ints, returning int
fn (string, bool) string  // Function taking string and bool, returning string
fn int                    // Function with no params, returning int (shorthand for fn () int)

// Type alias for function types
type BinaryOp = fn (a: int, b: int) int
```

### Type Declarations

```lambda
// Type aliases
type UserId = int;
type UserName = string;
type Point = (float, float);

// Object types
type User = {
    id: UserId,
    name: UserName,
    email: string,
    active: bool
};

// Element types
type Article = <article title: string, author: string;
    string,           // Text content
    [Section]         // Array of sections
>;

type Section = <section heading: string;
    string            // Section content
>;
```

### Type Occurrences

```lambda
// Optional types
int?               // Optional integer (int | null)
string?            // Optional string (string | null)

// Array types with occurrences
int*               // Array of zero or more integers
string+            // Array of one or more strings
```

---

## System Functions

Lambda Script provides a comprehensive set of built-in system functions for type conversion, mathematical operations, collection manipulation, and I/O.

> **Full Documentation**: See [Lambda_Sys_Func_Reference.md](Lambda_Sys_Func_Reference.md) for complete system function documentation.

### Method-Style Calls

System functions can be called using **method-style syntax**, where the first argument becomes the receiver:

```lambda
// Traditional prefix style
len(arr)                    // 5
sum([1, 2, 3])              // 6
slice("hello", 0, 3)        // "hel"

// Method style (equivalent)
arr.len()                   // 5
[1, 2, 3].sum()             // 6
"hello".slice(0, 3)         // "hel"
```

The transformation is simple: `obj.method(args...)` becomes `method(obj, args...)`.

#### Supported Functions

Most system functions that take an object as their first parameter support method-style calls:

| Category | Prefix Style | Method Style |
|----------|--------------|-------------|
| **Type** | `len(arr)` | `arr.len()` |
| **Type** | `type(val)` | `val.type()` |
| **Type** | `string(42)` | `42.string()` |
| **Type** | `int("123")` | `"123".int()` |
| **String** | `slice(s, 0, 5)` | `s.slice(0, 5)` |
| **String** | `contains(s, "x")` | `s.contains("x")` |
| **String** | `starts_with(s, "a")` | `s.starts_with("a")` |
| **Collection** | `reverse(arr)` | `arr.reverse()` |
| **Collection** | `sort(arr)` | `arr.sort()` |
| **Collection** | `unique(arr)` | `arr.unique()` |
| **Collection** | `take(arr, 3)` | `arr.take(3)` |
| **Collection** | `drop(arr, 2)` | `arr.drop(2)` |
| **Stats** | `sum(nums)` | `nums.sum()` |
| **Stats** | `min(nums)` | `nums.min()` |
| **Stats** | `max(nums)` | `nums.max()` |
| **Stats** | `avg(nums)` | `nums.avg()` |
| **Math** | `abs(x)` | `x.abs()` |
| **Math** | `round(x)` | `x.round()` |
| **Math** | `floor(x)` | `x.floor()` |
| **Math** | `ceil(x)` | `x.ceil()` |
| **Math** | `sqrt(x)` | `x.sqrt()` |

#### Method Chaining

Method-style syntax enables fluent, readable chained operations:

```lambda
// Chained method calls - left to right flow
let result = data
    .filter(x => x > 0)
    .map(x => x * 2)
    .sort()
    .take(10)
    .sum()

// Equivalent prefix style - nested, harder to read
let result = sum(take(sort(map(filter(data, x => x > 0), x => x * 2)), 10))
```

#### Examples

```lambda
// String operations
let s = "Hello, World!"
s.len()                     // 13
s.contains("World")         // true
s.starts_with("Hello")      // true
s.slice(0, 5)               // "Hello"

// Array operations
let nums = [3, 1, 4, 1, 5, 9, 2, 6]
nums.len()                  // 8
nums.sum()                  // 31
nums.min()                  // 1
nums.max()                  // 9
nums.sort()                 // [1, 1, 2, 3, 4, 5, 6, 9]
nums.unique()               // [3, 1, 4, 5, 9, 2, 6]
nums.reverse()              // [6, 2, 9, 5, 1, 4, 1, 3]

// Chained operations
[5, 3, 1, 4, 2].sort().reverse()  // [5, 4, 3, 2, 1]

// Type conversion
42.string()                 // "42"
"123".int()                 // 123
3.14.floor()                // 3
(-5).abs()                  // 5
```

**Note**: Both prefix and method styles are fully interchangeable. Choose the style that best fits your code's readability.

### Function Categories

| Category | Functions | Description |
|----------|-----------|-------------|
| **Type** | `type`, `len`, `int`, `float`, `string`, `symbol`, ... | Type conversion and inspection |
| **Math** | `abs`, `round`, `floor`, `ceil`, `sqrt`, `log`, `sin`, `cos`, ... | Mathematical operations |
| **Statistical** | `sum`, `avg`, `mean`, `median`, `variance`, `deviation`, `quantile` | Statistical analysis |
| **Collection** | `slice`, `set`, `all`, `any`, `reverse`, `sort`, `unique`, `concat`, ... | Collection manipulation |
| **Vector** | `cumsum`, `cumprod`, `argmin`, `argmax`, `dot`, `norm`, `fill`, `range` | Vector operations |
| **I/O** | `input`, `format`, `print`, `output`, `fetch` | Input/output operations |
| **Date/Time** | `datetime`, `date`, `time`, `today`, `now`, `justnow` | Date and time functions |
| **Error** | `error` | Error handling |
| **Variadic** | `varg` | Variadic argument access |

---

## Operators

### Arithmetic Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `+` | Addition | `5 + 3` | `8` |
| `-` | Subtraction | `5 - 3` | `2` |
| `*` | Multiplication | `5 * 3` | `15` |
| `/` | Division | `10 / 3` | `3.333...` |
| `div` | Integer division | `10 div 3` | `3` |
| `%` | Modulo | `17 % 5` | `2` |
| `^` | Exponentiation | `2 ^ 3` | `8` |

### Comparison Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `==` | Equal | `5 == 5` | `true` |
| `!=` | Not equal | `5 != 3` | `true` |
| `<` | Less than | `3 < 5` | `true` |
| `<=` | Less than or equal | `5 <= 5` | `true` |
| `>` | Greater than | `5 > 3` | `true` |
| `>=` | Greater than or equal | `5 >= 3` | `true` |

### Logical Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `and` | Logical AND | `true and false` | `false` |
| `or` | Logical OR | `true or false` | `true` |
| `not` | Logical NOT | `not true` | `false` |

### Unary Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `-` | Negation | `-5` | `-5` |
| `+` | Positive | `+5` | `5` |
| `not` | Logical NOT | `not true` | `false` |

### Set Operators

| Operator | Description  | Example       |
| -------- | ------------ | ------------- |
| `&`      | Intersection | `set1 & set2` |
| `!`      | Exclusion    | `set1 ! set2` |

### Pipe and Filter Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `\|` | Pipe (transform/map) | `[1, 2, 3] \| ~ * 2` | `[2, 4, 6]` |
| `where` | Filter | `[1, 2, 3, 4] where ~ > 2` | `[3, 4]` |

The pipe operator `|` enables fluent data transformation pipelines:
- **With `~`**: Auto-maps the expression over each item in a collection
- **Without `~`**: Passes the entire collection as the first argument to a function

```lambda
// Map pipe (with ~)
[1, 2, 3] | ~ * 2           // [2, 4, 6]
users | ~.name              // ["Alice", "Bob", ...]

// Aggregate pipe (without ~)
[3, 1, 4] | sort            // [1, 3, 4]
[1, 2, 3] | sum             // 6

// Combined with where
[1, 2, 3, 4, 5] | ~ ^ 2 where ~ > 10 | sum
// [1, 4, 9, 16, 25] → [16, 25] → 41
```

See [Pipe Expressions](#pipe-expressions) for detailed documentation.

### Type Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `is` | Type check | `42 is int` | `true` |
| `in` | Membership | `2 in [1, 2, 3]` | `true` |
| `to` | Range | `1 to 5` | `[1, 2, 3, 4, 5]` |

### Operator Precedence

From highest to lowest precedence:

1. Primary expressions (`()`, `[]`, `.`)
2. Unary operators (`-`, `+`, `not`)
3. Exponentiation (`^`)
4. Multiplicative (`*`, `/`, `div`, `%`)
5. Additive (`+`, `-`)
6. Relational (`<`, `<=`, `>`, `>=`)
7. Equality (`==`, `!=`)
8. Logical AND (`and`)
9. Logical OR (`or`)
10. Range (`to`)
11. Type operations (`is`, `in`)
12. Pipe (`|`)
13. Filter (`where`)

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

Each Lambda Script file is a module that can export public declarations:

```lambda
// math_utils.ls
pub PI = 3.14159;
pub E = 2.71828;

pub fn square(x: float) => x * x;
pub fn cube(x: float) => x * x * x;

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

## Error Handling

### Error Values

Lambda Script uses explicit error values for error handling:

```lambda
// Creating errors
error("Something went wrong")
error("Division by zero in calculation")

// Functions that may return errors
fn safe_divide(a: float, b: float) float | error {
    if (b == 0.0) {
        error("Division by zero")
    } else {
        a / b
    }
}
```

### Error Propagation

```lambda
// Checking for errors
let result = safe_divide(10.0, 0.0);
if (result is error) {
    print("Error occurred:", result)
} else {
    print("Result:", result)
}

// Error handling in expressions
let value = if (x == 0) error("Invalid input") else process(x);
```

### Common Error Patterns

```lambda
// Type mismatches
5 + "hello"        // Error: cannot add int and string
true < false       // Error: cannot compare booleans

// Division by zero
5 / 0              // Error: division by zero
5 % 0              // Error: modulo by zero

// Index out of bounds
[1, 2, 3][5]       // Error: index out of bounds

// Invalid type operations
"hello" * 3.14     // Error: cannot multiply string by float
```

---

## Memory Management

Lambda Script uses automatic memory management with reference counting and memory pools:

### Reference Counting

- All values are automatically reference counted
- Memory is freed when reference count reaches zero
- No manual memory management required

### Memory Pools

- Objects are allocated from memory pools for efficiency
- Pools are automatically managed by the runtime
- Reduces fragmentation and improves performance

### Immutability

- Most data structures are immutable by default
- Immutability eliminates many memory safety issues
- Structural sharing for efficient memory usage

```lambda
// Immutable collections
let list1 = (1, 2, 3);
let list2 = (0, list1...);  // Shares structure with list1

// Mutable collections (arrays)
let arr = [1, 2, 3];
// arr is mutable, but assignment creates new references
```

---

## Examples

### Basic Data Processing

```lambda
// Read and process JSON data
let data = input("sales.json", 'json');

// Calculate total sales
let total = sum((for (item in data.sales) item.amount));

// Filter high-value sales
let high_value = (for (sale in data.sales) 
                     if (sale.amount > 1000) sale else null);

// Generate report
let report = {
    total_sales: total,
    high_value_count: len(high_value),
    average: total / len(data.sales),
    timestamp: datetime()
};

// Output as formatted JSON
print(format(report, 'json'));
```

### Document Processing

```lambda
// Parse Markdown document
let doc = input("article.md", 'markdown');

// Extract headings
let headings = (for (element in doc) 
                   if (element.tag == 'h1' or element.tag == 'h2') 
                       element.content 
                   else null);

// Generate table of contents
let toc = <div class: "toc";
    <h2; "Table of Contents">
    <ul;
        for (heading in headings) <li; <a href: "#" + heading; heading>>
    >
>;

// Create enhanced document
let enhanced = <article;
    toc;
    doc
>;

// Output as HTML
print(format(enhanced, 'html'));
```

### Mathematical Computation

```lambda
// Define mathematical functions
fn factorial(n: int) int {
    if (n <= 1) {
        1
    } else {
        n * factorial(n - 1)
    }
}

fn fibonacci(n: int) int {
    if (n <= 1) {
        n
    } else {
        fibonacci(n - 1) + fibonacci(n - 2)
    }
}

// Calculate sequences
let factorials = (for (i in 1 to 10) factorial(i));
let fibs = (for (i in 1 to 15) fibonacci(i));

// Generate mathematical report
let math_report = <div;
    <h1; "Mathematical Sequences">
    <h2; "Factorials">
    <p; "First 10 factorials: " + string(factorials)>
    <h2; "Fibonacci Numbers">  
    <p; "First 15 Fibonacci numbers: " + string(fibs)>
>;

print(format(math_report, 'html'));
```

### Type-Safe Configuration

```lambda
// Define configuration types
type DatabaseConfig = {
    host: string,
    port: int,
    database: string,
    ssl: bool
};

type ServerConfig = {
    listen_port: int,
    debug: bool,
    database: DatabaseConfig
};

// Load and validate configuration
let config_data = input("config.yaml", 'yaml');

// Type-safe configuration access
let server_config: ServerConfig = {
    listen_port: config_data.server.port,
    debug: config_data.server.debug,
    database: {
        host: config_data.database.host,
        port: config_data.database.port,
        database: config_data.database.name,
        ssl: config_data.database.ssl
    }
};

// Use configuration
if (server_config.debug) {
    print("Debug mode enabled");
    print("Database:", server_config.database.host);
}
```

### Advanced Data Transformation

```lambda
// Complex data transformation pipeline
let raw_data = input("customers.json", 'json');

// Multi-stage processing
let processed = (
    let customers = raw_data.customers,
    let active_customers = (for (c in customers) if (c.active) c else null),
    let enriched = (for (customer in active_customers) {
        ...customer,
        full_name: customer.first_name + " " + customer.last_name,
        account_age: datetime() - customer.created_at,
        lifetime_value: sum((for (order in customer.orders) order.amount))
    }),
    let high_value = (for (c in enriched) if (c.lifetime_value > 1000) c else null),
    let sorted = sort(high_value, fn(a, b) => b.lifetime_value - a.lifetime_value),
    sorted
);

// Generate customer report
let report = <html;
    <head; <title; "High-Value Customers">>
    <body;
        <h1; "Top Customers by Lifetime Value">
        <table;
            <tr; <th; "Name"> <th; "Email"> <th; "Lifetime Value">>
            for (customer in processed) <tr;
                <td; customer.full_name>
                <td; customer.email>
                <td; "$" + string(customer.lifetime_value)>
            >
        >
    >
>;

// Output formatted report
print(format(report, 'html'));
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
2. **Pattern Matching**: Destructuring in let expressions
3. **Type System**: Rich types including unions, intersections, and generics
4. **Document Processing**: Built-in support for markup and data formats

---

This reference provides a comprehensive overview of Lambda Script. The language continues to evolve, with new features and improvements being added regularly. For the latest updates and examples, refer to the test files in the `test/lambda/` directory and the official documentation.
