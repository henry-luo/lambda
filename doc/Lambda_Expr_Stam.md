# Lambda Expressions and Statements

This document covers Lambda's expressions and statements, including control flow, operators, and pipe expressions.

> **Related Documentation**:
> - [Lambda Reference](Lambda_Reference.md) — Language overview and syntax
> - [Lambda Data](Lambda_Data.md) — Literals and collections
> - [Lambda Type System](Lambda_Type.md) — Type hierarchy and patterns
> - [Lambda Functions](Lambda_Func.md) — Function definitions
> - [Lambda Procedural](Lambda_Procedural.md) — Procedural statements (`var`, `while`, assignment)

---

## Table of Contents

1. [Expression-Oriented Design](#expression-oriented-design)
2. [Primary Expressions](#primary-expressions)
3. [Arithmetic Expressions](#arithmetic-expressions)
4. [Comparison Expressions](#comparison-expressions)
5. [Logical Expressions](#logical-expressions)
6. [Member Access and Null Safety](#member-access-and-null-safety)
7. [Pipe Expressions](#pipe-expressions)
8. [Query Expressions](#query-expressions)
9. [Control Flow Expressions](#control-flow-expressions)
10. [Match Expressions](#match-expressions)
11. [Statements](#statements)
12. [Operators](#operators)

---

## Primary Expressions

### Literals

```lambda
// Literals are expressions
42
"hello"
true
null
[1, 2, 3]
{name: "Alice"}
```

### Variables

```lambda
// Variable references
x
myVariable
_underscore_name
```

### Parenthesized Expressions

```lambda
// Grouping for precedence
(x + y) * z

// Let expressions (parenthesized)
(let x = 42, x + 1)  // Returns 43
```

### Collection Access

```lambda
// Array/list index access
arr[0]
arr.1             // same as arr[1]
arr[1 to 4]       // Slice

// Map field access
map.key
map["key"]       // dynamic key
obj.nested.field

// Safe navigation (optional chaining) built-in
obj.maybeNull.field    // Returns null if maybeNull is null
```

---

## Arithmetic Expressions

### Basic Arithmetic

```lambda
5 + 3              // Addition: 8
5 - 3              // Subtraction: 2
5 * 3              // Multiplication: 15
10 / 3             // Division: 3.333...
10 div 3           // Integer division: 3
17 % 5             // Modulo: 2
2 ** 3              // Exponentiation: 8
```

### Unary Operators

```lambda
-x                 // Negation
+x                 // Positive (identity)
not x              // Logical NOT
!T                 // Type negation (any except T)
*x                 // Spread (expand collection)
```

### Spread Operator

The spread operator `*` expands a collection's items into the enclosing container:

```lambda
let a = [1, 2, 3]
[0, *a, 4]             // [0, 1, 2, 3, 4] — items spread into array

let b = (10, 20)
(*a, *b)               // (1, 2, 3, 10, 20) — spread into list

// Spread in function calls
fn sum_all(...args) = args | reduce((a, b) => a + b, 0)
sum_all(*[10, 20, 30])  // 60

// Nested spreading
let nested = [[1, 2], [3, 4]]
[*nested[0], *nested[1]]  // [1, 2, 3, 4]
```

### Vector Arithmetic

Lambda supports NumPy-style element-wise operations on arrays:

```lambda
// Scalar-vector operations
1 + [2, 3, 4]              // [3, 4, 5]
10 - [1, 2, 3]             // [9, 8, 7]
3 * [1, 2, 3]              // [3, 6, 9]
2 ** [1, 2, 3]              // [2, 4, 8]

// Vector-vector operations
[1, 2, 3] + [4, 5, 6]      // [5, 7, 9]
[10, 20, 30] - [1, 2, 3]   // [9, 18, 27]
[2, 3, 4] * [1, 2, 3]      // [2, 6, 12]

// Broadcasting
[5] + [1, 2, 3]            // [6, 7, 8]
```

---

## Comparison Expressions

### Equality

The `==` operator performs **structural deep value equality** for all types — scalars and containers alike.

```lambda
// Scalar equality
5 == 5             // true
5 != 3             // true
1 == 1.0           // true (numeric promotion: int, float, decimal)
```

#### Container Equality

Containers (lists, arrays, maps, elements) are compared by **structure**, not by reference. Two containers are equal if they have the same shape and all corresponding elements are equal:

```lambda
// List and array equality
[1, 2, 3] == [1, 2, 3]           // true
[1, 2, 3] == [1, 2, 4]           // false (element mismatch)
[1, 2, 3] == [1, 2]              // false (different length)
[] == []                          // true

// Numeric promotion composes into containers
[1] == [1.0]                      // true (element-wise: 1 == 1.0)
{a: 1} == {a: 1.0}                // true

// Map equality is order-independent
{a: 1, b: 2} == {b: 2, a: 1}     // true
{a: 1, b: 2} == {a: 1}           // false (different key count)

// Nested structural equality
[[1, 2], [3, 4]] == [[1, 2], [3, 4]]   // true
{a: {x: 1}} == {a: {x: 1}}             // true
{a: [1, 2]} == {a: [1, 2]}             // true
```

#### Cross-Type Sequence Equality

Ranges, lists, and arrays are all **sequences** — they compare equal across types if they contain the same elements:

```lambda
(1 to 3) == [1, 2, 3]            // true (range vs list)
[1, 2, 3] == (1 to 3)            // true (symmetric)
(1 to 3) == [1, 2, 4]            // false
(1 to 3) == [1, 2]               // false (different length)
```

#### Function Equality

Functions use **reference equality** — two function values are equal only if they are the same function object:

```lambda
fn add1(x) => x + 1
let f = add1
f == f                            // true (same reference)
```

#### NaN Equality

Follows IEEE 754: `NaN != NaN`, including inside containers:

```lambda
let x = 0.0 / 0.0
x == x                            // false
[x] == [x]                        // false
```

### Relational

```lambda
3 < 5              // Less than: true
5 > 3              // Greater than: true
3 <= 5             // Less than or equal: true
5 >= 3             // Greater than or equal: true
```

### Null Comparisons

Null can be compared with any type:

```lambda
null == null       // true
null == 42         // false (not an error)
"hello" != null    // true

// Idiomatic null check
if (x == null) "missing" else x
```

### Type Comparisons

```lambda
// Type checking
42 is int              // true
"hello" is string      // true
!(42 is string)        // true (negated type check)

// Type equality
type(42) == int        // true
type([1,2]) == array   // true
type(42) != string     // true
```

---

## Logical Expressions

### Boolean Operators

```lambda
true and false     // Logical AND: false
true or false      // Logical OR: true
not true           // Logical NOT: false
```

### Short-Circuit Evaluation

```lambda
// Right side only evaluated if needed
(x > 0) and (y / x > 2)    // Safe: y/x not evaluated if x <= 0
value or "default"          // Returns value if truthy, else "default"
```

### Truthy and Falsy Values

Lambda has simple truthiness rules:

| Falsy Values | Note                                                                 |
| ------------ | -------------------------------------------------------------------- |
| `null`       |                                                                      |
| `false`      |                                                                      |
| `error`      | error is falsy, which allows idiom like: `err or fallback`           |
| "", ''       | Empty string`""` or symbol `''` is normalised to `null` under Lambda |

| Truthy Values (Everything Else)                          |
| -------------------------------------------------------- |
| `true`, all numbers (including `0`)                      |
| All strings, symbols (Lambda string/symbol is non-empty) |
| All collections (including `[]`, `{}`)                   |
| All functions                                            |
**Important**: Unlike many languages, `0` and empty collections are **truthy** in Lambda.

```lambda
if (0) "yes" else "no"           // "yes" - 0 is truthy
if ([]) "yes" else "no"          // "yes" - empty array is truthy
if (null) "yes" else "no"        // "no" - null is falsy
if (false) "yes" else "no"       // "no" - false is falsy
if ("") "yes" else "no"          // "no" - empty string is falsy
```

---

## Member Access and Null Safety

### Dot Operator

The `.` operator accesses fields and has **built-in null safety**:

```lambda
// Field access
user.name
config.settings.theme

// Automatic null propagation
let x = null
x.name           // null (not an error)
x.a.b.c          // null (null propagates through chain)
```

### Safe Navigation

Lambda's `.` operator behaves like JavaScript's `?.` by default:

```lambda
// Given potentially null objects at any level
let result = company.department.manager.name

// Equivalent to verbose null-checking:
// if (company == null) null
// else if (company.department == null) null
// else if (company.department.manager == null) null
// else company.department.manager.name
```

### Index Access

```lambda
arr[0]             // First element
arr[-1]            // Last element
map["key"]         // Map value by key

// Null-safe index access
let data = null
data[0]            // null
data["key"]        // null

let arr = [1, 2, 3]
arr[10]            // null (out of bounds returns null)
```

### Method Calls

```lambda
// Method-style function calls
arr.len()          // Same as len(arr)
"hello".upper()    // Same as upper("hello")
items.sort()       // Same as sort(items)

// Null receiver returns null
let items = null
items.len()        // 0 (len of null is 0)
items.reverse()    // null
```

---

## Pipe Expressions

The pipe operator (`|`) enables fluent, left-to-right data transformation.

### Basic Pipe Syntax

```lambda
<collection> | <expression-with-~>
```

- **`|`** — Set-oriented pipe operator
- **`~`** — Current item reference

### Auto-Mapping Over Collections

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

### Scalar Pipe

When the left side is a scalar, `~` binds to the whole value:

```lambda
42 | ~ * 2
// Result: 84

"hello" | ~ ++ " world"
// Result: "hello world"
```

### Chained Transformations

```lambda
[1, 2, 3, 4, 5]
    | ~ ** 2           // square: [1, 4, 9, 16, 25]
    | ~ + 1           // add 1: [2, 5, 10, 17, 26]
// Result: [2, 5, 10, 17, 26]
```

### Key/Index Access with `~#`

```lambda
// Arrays — ~# is index (0-based)
['a', 'b', 'c'] | {index: ~#, value: ~}
// [{index: 0, value: 'a'}, {index: 1, value: 'b'}, {index: 2, value: 'c'}]

// Maps — ~ is value, ~# is key
{a: 1, b: 2} | {key: ~#, val: ~}
// [{key: 'a', val: 1}, {key: 'b', val: 2}]
```

### Aggregated Pipe (without `~`)

When `~` is not used, the pipe passes the entire collection/data on left side to right side:

```lambda
[3, 1, 4, 1, 5] | sum        // 14
[3, 1, 4, 1, 5] | sort       // [1, 1, 3, 4, 5]
[1, 2, 3, 4, 5] | take(3)    // [1, 2, 3]
```

### That Clause (Filtering)

```lambda
// Basic filtering
[1, 2, 3, 4, 5] that (~ > 3)
// Result: [4, 5]

// Filter objects
users that (age >= 18)
// Keep only adult users

// == and != work without parens
[1, 2, 3, 4, 5] that ~ == 3
// Result: [3]

// Combined with pipe
data | ~.name that (len(~) > 3) | ~.upper()
```

> **Note:** The relational operators `<`, `>`, `<=`, `>=` conflict with element-tag
> syntax in the parser. When a `that` (or `|`) condition uses any of these
> operators, wrap the condition in parentheses: `items that (~ > 0)`.  
> The operators `==`, `!=`, `and`, `or`, `+`, `-`, `*`, `/` work without parens.

#### Implicit Field Access in `that` Clause

Inside a `that` clause, bare identifiers that are not in scope automatically resolve to `~.name` — **implicit field access**:

```lambda
// Explicit: ~.field
users that (~.age >= 18 and ~.name != "admin")

// Implicit: bare field names
users that (age >= 18 and name != "admin")

// Both forms produce identical results
```

Name resolution order inside a `that` clause:
1. Names in scope (`let`, `var`, `fn`, `pn`, `type` definitions)
2. Stored field on the current item `~` (map/object/element)
3. System properties of the current item `~`

```lambda
let min_age = 18
// 'min_age' resolves to the let binding; 'age' resolves to ~.age
users that (age >= min_age)
```

### Pipe Behavior Summary

| Left Side | `~` Binds To | `~#` Binds To | Result |
|-----------|--------------|---------------|--------|
| `[a, b, c]` (array) | Each element | Index (0, 1, 2) | Array of results |
| `(a, b, c)` (list) | Each element | Index (0, 1, 2) | List of results |
| `1 to 10` (range) | Each number | Position (0-9) | Array of results |
| `{a: 1, b: 2}` (map) | Each value | Key ('a', 'b') | Collection of results |
| `42` (scalar) | The value itself | N/A | Single result |

---

## Query Expressions

Lambda provides type-based query operators for searching nested data structures — elements, maps, arrays, and lists.

### Recursive Query: `?` and `.?`

The `?` operator performs a **recursive descendant search**, returning all values at any depth that match the given type:

```lambda
html?<img>                  // all <img> elements at any depth
data?int                    // all int values in the tree
data?(int | string)         // all int or string values
html?<div class: string>    // <div> elements with a class attribute
data?{name: string}         // maps with a string 'name' field
data?{status: "ok"}         // maps where status == "ok"
```

The `.?` variant is **self-inclusive** — it also tests the root value itself:

```lambda
div.?<div>                  // includes div itself if it matches
42.?int                     // (42) — trivial self-match
el.?int                     // self + all int values in subtree
```

Both operators traverse attributes, children, map values, and array/list items in **document order** (depth-first, pre-order). Results are returned as a spreadable array.

### Child-Level Query: `[T]`

The `[T]` child-level query searches only **direct** attributes and children — one level deep, no recursion:

```lambda
[1, "hello", 3, true][int]        // (1, 3) — direct int items
{name: "Alice", age: 30}[string]  // ("Alice") — map values matching type
el[element]                       // direct child elements only
el[string]                        // attribute values + text children
```

The `[T]` syntax reuses the index operator `expr[x]`. When `x` is a **type value**, a child-level query is performed instead of normal index access:

| Index value `x` | Interpretation |
|-----------------|----------------|
| `int` value | Positional index access |
| `string` or `symbol` value | Named field access |
| Type | Child-level query |

On **elements**, `[T]` searches both attribute values and direct children. On **maps**, it searches values only. On **arrays** and **lists**, it searches items.

#### Chaining

Child-level queries can be chained for multi-level traversal, and mixed with `?` for combined specific/recursive search:

```lambda
type body = <body>
type div  = <div>

html[body][div]                // direct <div> children of <body>
html[body][div]?<a>            // then recursive search for <a>
html?<table>[tr][td]           // all tables → direct rows → direct cells
```

#### Comparison

| Feature | `expr?T` | `expr[T]` |
|---------|----------|----------|
| Scope | All descendants (recursive) | Direct attributes + children only |
| Depth | Unlimited | One level |
| Self-inclusive | `.?T` | N/A |
| Analogy | XPath `//`, CSS descendant | XPath `/`, CSS `>` child |
| Return type | Spreadable array | Spreadable array |

---

## Control Flow Expressions

### If Expressions

Lambda has two `if` forms that share the same `else` syntax and produce the same AST node.

#### Expression Form: `if (cond) expr else ...`

The parenthesized condition form requires `else` and returns a value:

```lambda
// Simple if expression
let result = if (x > 0) "positive" else "non-positive"

// Nested if expressions
let grade = if (score >= 90) "A"
            else if (score >= 80) "B"
            else if (score >= 70) "C"
            else "F"

// If in let bindings
(let x = 5, if (x > 3) "big" else "small")

// Block else (NEW) — else branch can be a { stam } block
let msg = if (x > 0) "ok" else {
    let reason = diagnose(x);
    "error: " ++ reason
}
```

#### Block Form: `if cond { stam } [else ...]`

The block form uses `{ stam }` for the then-branch. `else` is optional:

```lambda
// Block if, no else
if x > 0 { print("positive") }

// Block if with block else
if temperature > 30 {
    print("hot")
} else {
    print("comfortable")
}

// Expression else (NEW) — else branch can be an expression
if x > 0 { compute(x) } else default_value

// Chaining
if x > 0 {
    "positive"
} else if x < 0 {
    "negative"
} else {
    "zero"
}
```

#### Unified Else Branch

Both forms accept the same `else` options:

| Else form | Example |
|---|---|
| `else expr` | `else "fallback"` |
| `else { stam }` | `else { let x = calc(); x }` |
| `else if ...` | `else if (y > 0) ...` or `else if y > 0 { ... }` |

When `else` is omitted (block form only), the result is `null`.

> **Map/block ambiguity:** `else { ... }` is always parsed as a block, not a map literal. To return a map from an else branch, use parentheses: `else ({a: 1, b: 2})`.

### For Expressions

For expressions produce **spreadable arrays** that automatically flatten when nested in collections:

```lambda
// Basic iteration - produces spreadable array
for (x in [1, 2, 3]) x * 2    // [2, 4, 6]

// Range iteration
for (i in 1 to 5) i * i       // [1, 4, 9, 16, 25]

// Conditional in body
for (num in [1, 2, 3, 4, 5])
    if (num % 2 == 0) num else null
```

#### Map Iteration with `at`

Use the `at` keyword (instead of `in`) to iterate over map keys or key-value pairs. This works with both **static maps** (literal `{...}`) and **dynamic maps** (created with `map([...])`).

**Keys only** — `for (k at map)`:

```lambda
for (k at {a: 1, b: 2, c: 3}) k
// ["a", "b", "c"]

let m = map(["x", 10, "y", 20])
for (k at m) k
// ["x", "y"]
```

**Key-value pairs** — `for (k, v at map)`:

```lambda
for (k, v at {a: 1, b: 2, c: 3}) k ++ "=" ++ string(v)
// ["a=1", "b=2", "c=3"]

let scores = map(["alice", 95, "bob", 87])
for (name, score at scores) {name: name, score: score}
// [{name: "alice", score: 95}, {name: "bob", score: 87}]
```

**With `where` clause**:

```lambda
for (k, v at {a: 1, b: 5, c: 2} where v > 2) k
// ["b"]
```

> **Note**: Use `in` to iterate over arrays and lists; use `at` to iterate over maps.

#### Spreadable Array Behavior

For expressions produce spreadable arrays that flatten when nested in other collections:

```lambda
// Nested for-expressions flatten automatically
[for (i in 1 to 3) for (j in 1 to 3) i * j]
// [1, 2, 3, 2, 4, 6, 3, 6, 9] — flat array, not nested

// Spreading into array literals
[0, for (x in [1, 2, 3]) x * 10, 99]
// [0, 10, 20, 30, 99] — for-expr items spread into the array

// Spreading into lists
(0, for (x in [1, 2]) x * 5, 99)
// (0, 5, 10, 99)

// Multiple for-expressions spread independently
[for (x in [1, 2]) x, for (y in [3, 4]) y * 10]
// [1, 2, 30, 40]
```

#### Empty For Results

When a for-expression iterates over an empty collection or filters all elements, it produces a **spreadable null** that is skipped when spreading:

```lambda
// Empty iteration produces spreadable null (evaluates to null)
let v = for (i in []) i
v == null              // true

// Spreadable null is skipped in collections
[for (i in []) i]      // [] — empty array, not [null]
[1, for (i in []) i, 2]  // [1, 2] — null skipped

// Where clause filters all elements
[for (x in [1, 2, 3] where x > 100) x]  // []
```

### Extended For-Expression Clauses

For expressions support SQL/XQuery-inspired clauses for filtering, sorting, pagination, and intermediate bindings. The full syntax is:

```
for (<bindings> [, let <name> = <expr>, ...] [where <cond>] [order by <spec>] [limit <n>] [offset <n>]) <body>
```

Clauses are processed in logical order: **bindings → let → where → order by → offset → limit → body**.

#### `where` — Filter

The `where` clause filters items by a boolean condition. Only items where the condition is truthy proceed to the body. Use `and` / `or` to combine conditions.

```lambda
for (x in [1, 2, 3, 4, 5] where x > 2) x
// (3, 4, 5)

for (user in users where user.active and user.age >= 18)
  user.name

// where eliminates nulls cleanly — compare with if in body:
for (x in data where x > 0) x * 2     // [2, 4, 6]
for (x in data) if (x > 0) x * 2      // [null, 2, null, 4, ...] — nulls remain
```

#### `let` — Intermediate Bindings

The `let` clause introduces named values computed per iteration. The name is available in subsequent clauses (`where`, `order by`) and in the body. Multiple `let` clauses are comma-separated after the bindings, and each can reference earlier `let` names.

```lambda
// Compute once, reuse in where and body
for (x in [1, 2, 3], let squared = x * x) squared + 1
// (2, 5, 10)

// let feeds into where
for (x in [1, 2, 3, 4, 5], let doubled = x * 2 where doubled > 4) doubled
// (6, 8, 10)

// Chained lets — each sees the previous
for (x in [2, 3, 4], let sq = x * x, let cube = sq * x where cube > 10) [x, sq, cube]
// [[3, 9, 27], [4, 16, 64]]

// let avoids redundant computation
for (order in orders,
     let subtotal = sum(order.items | ~.price * ~.qty),
     let tax = subtotal * 0.08,
     let total = subtotal + tax
     where total > 100)
  {id: order.id, total: total}
```

#### `order by` — Sort

The `order by` clause sorts the result set. The default direction is ascending. Append `desc` for descending, or `asc` for explicit ascending. Multiple sort keys are comma-separated for tie-breaking.

```lambda
// Ascending (default)
for (x in [3, 1, 4, 1, 5] order by x) x
// (1, 1, 3, 4, 5)

// Descending
for (x in [3, 1, 4, 1, 5] order by x desc) x
// (5, 4, 3, 1, 1)

// Sort by field
let people = [{name: "Alice", age: 30}, {name: "Bob", age: 25}, {name: "Carol", age: 35}]
for (p in people order by p.age) p.name
// ("Bob", "Alice", "Carol")

// Sort by computed expression
for (s in ["banana", "fig", "apple"] order by len(s)) s
// ("fig", "apple", "banana")

// Multiple sort keys (secondary for tie-breaking)
for (p in employees order by p.department asc, p.salary desc)
  {name: p.name, dept: p.department}
```

#### `limit` and `offset` — Pagination

`limit N` returns at most N results. `offset M` skips the first M results. Both are applied **after** filtering and sorting.

```lambda
// First 3 items
for (x in [1, 2, 3, 4, 5] limit 3) x
// (1, 2, 3)

// Skip first 2
for (x in [1, 2, 3, 4, 5] offset 2) x
// (3, 4, 5)

// Combined: skip 2, then take 3
for (x in [1, 2, 3, 4, 5, 6, 7] limit 3 offset 2) x
// (3, 4, 5)

// Pagination pattern: page 3, 20 items per page
for (item in items order by item.id limit 20 offset 40) item

// Offset past end returns empty
for (x in [1, 2, 3] offset 10) x
// (empty)

// Limit larger than collection returns all
for (x in [1, 2, 3] limit 10) x
// (1, 2, 3)
```

#### `group by` — Grouping

> **Status:** The `group by` clause is parsed but **not yet implemented** in the runtime. It is reserved for future use.

The planned syntax groups items by a key expression and binds each group to a name via `as`:

```
group by <key-expr> [, <key-expr>, ...] as <group-name>
```

Each group will be a map with `.key` (the grouping key) and `.items` (array of items in that group):

```lambda
// Planned syntax (not yet available):
for (sale in sales group by sale.product as g)
  {product: g.key, total: sum(g.items | ~.amount)}
```

#### Combined Clauses

All clauses can be used together. The recommended order matches the logical processing order:

```lambda
for (x in items,
     let score = x.value * x.weight
     where x.active
     order by score desc
     limit 10)
  {name: x.name, score: score}

// Filter, compute, sort, paginate in one expression
for (x in 1 to 20,
     let sq = x * x
     where x > 3 and x < 15
     order by sq desc
     limit 3 offset 2)
  sq
// (144, 121, 100)
```

---

## Match Expressions

The `match` expression provides multi-way branching based on type or value patterns. It is an expression that produces a value, and works in both functional and procedural contexts.

### Syntax

```
match <expr> {
    case <type_expr>: <expr>            // expression arm
    case <type_expr> { <statements> }   // statement arm
    default: <expr>                     // default expression arm
    default { <statements> }            // default statement arm
}
```

- **Braces are required** around the arm block.
- Parentheses around the scrutinee are optional: `match (expr) { ... }` and `match expr { ... }` are both valid.
- Expression and statement arms can be freely mixed within one match.
- `~` refers to the matched value inside arm bodies, like in pipe expressions.
- Arms are tested top-to-bottom; the first matching arm is selected.
- `default` is the catch-all arm (matches anything not matched by previous arms).

### Type Patterns

Match on the runtime type using type expressions:

```lambda
fn describe(value: int | string | bool) => match value {
    case int: "integer"
    case string: "text"
    case bool: "boolean"
}
```

### Literal Patterns

Literal values (integers, floats, booleans, strings, symbols, null) work as case patterns. The case checks equality against the literal value:

```lambda
fn status_text(code: int) => match code {
    case 200: "OK"
    case 404: "Not Found"
    case 500: "Server Error"
    default: "Unknown"
}
```

### Symbol Patterns

```lambda
fn color_of(level) => match level {
    case 'info': "blue"
    case 'warn': "yellow"
    case 'error': "red"
    default: "white"
}
```

### Or-Patterns

Combine multiple patterns into a single arm using `|`:

```lambda
fn day_type(day) => match day {
    case 'mon' | 'tue' | 'wed' | 'thu' | 'fri': "weekday"
    case 'sat' | 'sun': "weekend"
}
```

### Current Item Reference (`~`)

Inside match arms, `~` refers to the matched value:

```lambda
fn check_range(n: int) => match n {
    case 0: "zero"
    case int: if (~ > 0) "positive" else "negative"
}
```

### Mixed Expression and Statement Arms

Expression arms (`case T: expr`) and statement arms (`case T { stmts }`) can be freely combined:

```lambda
fn describe(shape) => match shape.tag {
    case 'circle' {
        let area = 3.14159 * shape.r ** 2;
        "circle with area " ++ string(area)
    }
    case 'rect' {
        let area = shape.w * shape.h;
        "rectangle with area " ++ string(area)
    }
    default: "unknown shape"
}
```

### Nested Match

```lambda
fn classify(value) => match value {
    case int: match value {
        case 0: "zero"
        default: "nonzero int"
    }
    case string: "string"
    default: "other"
}
```

### Match in Procedural Context

Match works in procedural functions with statement arms supporting `var`, `while`, `break`, `continue`, `return`:

```lambda
pn handle(event) {
    match event.kind {
        case 'click' {
            var count = state.clicks;
            count = count + 1;
            update_state({clicks: count})
        }
        case 'keypress' {
            if (event.key == "Escape") return null;
            process_key(event.key)
        }
        default: null
    }
}
```

### Match in Let Bindings

```lambda
let label = match status {
    case 'ok': "success"
    case 'warn': "warning"
    case 'error': "failure"
}
```

---

## Statements

### Common Statements

`let`, `if` and `for` statements work in both functional and procedural context.

| Construct | Expression Form             | Statement Form                  |
| --------- | --------------------------- | ------------------------------- |
| If        | `if (cond) a else b`        | `if (cond) { ... }`            |
| For       | `for (x in col) expr`       | `for x in col { ... }`         |
| Match     | `match x { case T: expr }`  | `match x { case T { ... } }`   |
| Let       | `(let x = 1, x + 1)`       | `let x = 1;`                   |

#### Let Statements

```lambda
// Variable declaration
let x = 42;
let name = "Alice", age = 30;

// With type annotation
let x: int = 42;
let items: string[] = ["a", "b", "c"];
```

#### If Statements

If statements use the block form described in [If Expressions](#if-expressions). Both forms produce the same AST node and can appear in statement position:

```lambda
// Block form (else optional)
if x > 0 {
    print("positive")
}

if temperature > 30 {
    print("hot")
} else {
    print("comfortable")
}

// Expression else in statement position
if x > 0 { print("ok") } else print("fail")
```

#### For Statements

For statements (with curly braces) also produce spreadable arrays:

```lambda
for item in [1, 2, 3] {
    print(item)
}

for i in 1 to 10 {
    if (i % 2 == 0) {
        print(i, "is even")
    }
}

// Nested for-statements flatten like for-expressions
let matrix = [[1, 2], [3, 4]]
for row in matrix {
    for col in row {
        col * 2
    }
}
// Produces: 2, 4, 6, 8 (flattened)

// Multiple loop variables
for x in [1, 2], y in [3, 4] {
    print(x, y)
}
```

### Procedural Statements

`var`, `while`, `break`, `continue`, `return`, and assignment (`=`) are only available in `pn` (procedural) functions. See [Lambda Procedural Programming](Lambda_Procedural.md) for full documentation.

---

## Operators

### Operator Precedence

From highest to lowest:

| Precedence | Operators                  | Description     |
| ---------- | -------------------------- | --------------- |
| 1          | `()`, `[]`, `[T]`, `.`, `?`, `.?` | Primary, query  |
| 2          | `-`, `+`, `not`, `!`, `*`  | Unary (`!`: type negation) |
| 3          | `**`                       | Exponentiation  |
| 4          | `*`, `/`, `div`, `%`       | Multiplicative  |
| 5          | `+`, `-`                   | Additive        |
| 6          | `<`, `<=`, `>`, `>=`       | Relational      |
| 7          | `==`, `!=`                 | Equality        |
| 8          | `and`                      | Logical AND     |
| 9          | `or`                       | Logical OR      |
| 10         | `to`                       | Range           |
| 11         | `is`, `in`                 | Type operations |
| 12         | `\|`, `that`               | Pipe, Filter    |

### Arithmetic Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `+` | Addition | `5 + 3` | `8` |
| `-` | Subtraction | `5 - 3` | `2` |
| `*` | Multiplication | `5 * 3` | `15` |
| `/` | Division | `10 / 3` | `3.333...` |
| `div` | Integer division | `10 div 3` | `3` |
| `%` | Modulo | `17 % 5` | `2` |
| `**` | Exponentiation | `2 ** 3` | `8` |

### Comparison Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `==` | Equal | `5 == 5` | `true` |
| `!=` | Not equal | `5 != 3` | `true` |
| `<` | Less than | `3 < 5` | `true` |
| `<=` | Less or equal | `5 <= 5` | `true` |
| `>` | Greater than | `5 > 3` | `true` |
| `>=` | Greater or equal | `5 >= 3` | `true` |

### Logical Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `and` | Logical AND | `true and false` | `false` |
| `or` | Logical OR | `true or false` | `true` |
| `not` | Logical NOT | `not true` | `false` |

### Set Operators

| Operator | Description  | Example        |
| -------- | ------------ | -------------- |
| `&`      | Intersection | `set1 & set2`  |
| `\|`     | Union        | `set1 \| set2` |
| `!`      | Exclusion    | `set1 ! set2`  |

### Type Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `is` | Type check | `42 is int` | `true` |
| `in` | Membership | `2 in [1, 2, 3]` | `true` |
| `to` | Range | `1 to 5` | `[1, 2, 3, 4, 5]` |

### Pipe and Filter Operators

| Operator | Description | Example | Result |
|----------|-------------|---------|--------|
| `\|` | Pipe (transform) | `[1, 2, 3] \| ~ * 2` | `[2, 4, 6]` |
| `that` | Filter | `[1, 2, 3, 4] that (~ > 2)` | `[3, 4]` |

### Pipe Output Operators

Available only in procedural (`pn`) functions. See [Lambda Procedural Programming](Lambda_Procedural.md#file-output-operators).

| Operator | Description | Mode |
|----------|-------------|------|
| `\|>` | Pipe to file | Write (truncate) |
| `\|>>` | Pipe append | Append |

### String/Collection Operators

| Operator | Description   | Example       | Result         |
| -------- | ------------- | ------------- | -------------- |
| `++`     | Concatenation | `"a" ++ "b"`  | `"ab"`         |
| `++`     | Array concat  | `[1] ++ [2]`  | `[1, 2]`       |

---

This document covers Lambda's expression and statement system. For function definitions, see [Lambda Functions](Lambda_Func.md). For type details, see [Lambda Type System](Lambda_Type.md). For procedural programming (`var`, `while`, assignment), see [Lambda Procedural](Lambda_Procedural.md).
