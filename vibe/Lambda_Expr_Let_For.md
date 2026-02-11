# Lambda Script: For and Let Expressions

This document describes the `for` and `let` expression and statement syntax in Lambda Script, along with proposed design changes for enhanced iteration and decomposition patterns.

---

## Table of Contents

1. [Current Syntax](#current-syntax)
   - [Let Expressions](#let-expressions)
   - [Let Statements](#let-statements)
   - [For Expressions](#for-expressions)
   - [For Statements](#for-statements)
2. [Proposed Design Changes](#proposed-design-changes)
   - [Indexed Iteration](#indexed-iteration-for-i-v-in-expr)
   - [Attribute Iteration](#attribute-iteration-for-k-v-at-expr)
   - [Positional Decomposition](#positional-decomposition-let-a-b--expr)
   - [Named Decomposition](#named-decomposition-let-a-b-at-expr)
   - [For-Expression Semantic Change](#for-expression-semantic-change)

---

## Current Syntax

### Let Expressions

Let expressions introduce local bindings within an expression context. They bind a name to a value and allow that binding to be used in subsequent expressions.

#### Syntax

```
let <name> = <expr>
```

#### Semantics

- Creates an immutable binding of `<name>` to the evaluated value of `<expr>`
- The binding is visible in subsequent expressions within the same scope
- Let expressions return the bound value

#### Examples

```lambda
// single binding in a list expression
(let x = 42, x + 1)  // Returns 43

// multiple bindings chained together
(let a = 1, let b = 2, a + b)  // Returns 3

// complex expressions with let
(let data = [1, 2, 3], 
 let doubled = (for (x in data) x * 2),
 doubled)  // Returns [2, 4, 6]

// let with type annotation
(let x: int = 42, x * 2)  // Returns 84
```

### Let Statements

Let statements declare variables at the statement level, typically at the top-level or within function bodies.

#### Syntax

```
let <name> = <expr>;
let <name1> = <expr1>, <name2> = <expr2>;
```

#### Semantics

- Creates immutable bindings in the current scope
- Multiple bindings can be declared in a single statement separated by commas
- Statement is terminated by `;` or newline

#### Examples

```lambda
let x = 42;
let y = 3.14, z = true;
let name = "Alice", age = 30;

// with type annotations
let x: int = 42;
let name: string = "Alice";
let items: [int] = [1, 2, 3];
```

### For Expressions

For expressions iterate over collections and produce new collections. They are the primary mechanism for functional iteration in Lambda Script.

#### Syntax

```
for (<name> in <collection>) <body-expr>
for (<name1> in <coll1>, <name2> in <coll2>) <body-expr>
```

#### Semantics

- Iterates over each element in the collection
- Evaluates `<body-expr>` for each element
- **Currently produces a list** containing all results
- Multiple loop variables create nested iteration (cartesian product)

#### Examples

```lambda
// array iteration
(for (x in [1, 2, 3]) x * 2)  // [2, 4, 6]

// range iteration
(for (i in 1 to 5) i * i)  // [1, 4, 9, 16, 25]

// conditional iteration
(for (num in [1, 2, 3, 4, 5]) 
    if (num % 2 == 0) num else 0)  // [0, 2, 0, 4, 0]

// string iteration
(for (item in ["a", "b", "c"]) item ++ "!")  // ["a!", "b!", "c!"]

// multiple loop variables (cartesian product)
(for (x in [1, 2], y in [10, 20]) x + y)  // [11, 21, 12, 22]
```

### For Statements

For statements iterate over collections with block syntax. Their behavior differs between functional and procedural contexts.

#### Syntax

```
for <name> in <collection> {
    <statements>
}
```

#### Semantics

- Iterates over each element in the collection
- Executes the statement body for each element
- **In functional `fn`**: Produces a list of content/items (each iteration contributes to the result)
- **In procedural `pn`**: Iterates without producing a collection (used for side effects)

#### Examples

```lambda
// In functional context (fn): produces a list
fn double_all(items) {
    for item in items {
        item * 2
    }
}
double_all([1, 2, 3])  // (2, 4, 6)

// In procedural context (pn): side effects only
pn print_all(items) {
    for item in items {
        print(item)
    }
}

for i in 1 to 10 {
    if (i % 2 == 0) {
        print(i, "is even")
    }
}

// multiple loop variables
for x in [1, 2], y in [3, 4] {
    print(x, y)
}
```

---

## Proposed Design Changes

### Indexed Iteration: `for i, v in expr`

Provides access to the iteration index alongside the value.

#### Syntax

```
for <index>, <value> in <expr>
```

#### Semantics

- `<index>`: The 0-based index of the current iteration
- `<value>`: The current element value
- Works with arrays, lists, ranges, and other iterables

#### Examples

```lambda
// array with index
(for (i, v in [10, 20, 30]) {index: i, value: v})
// [{index: 0, value: 10}, {index: 1, value: 20}, {index: 2, value: 30}]

// enumerate items
for i, item in ["a", "b", "c"] {
    print(i, ": ", item)  // 0: a, 1: b, 2: c
}
```

#### Map Iteration: `for k, v in map`

When iterating over a map, the two-variable form provides key-value pairs.

```lambda
let m = {name: "Alice", age: 30}

// iterate over key-value pairs
(for (k, v in m) k ++ ": " ++ str(v))
// ["name: Alice", "age: 30"]

for k, v in m {
    print(k, "=", v)
}
```

#### Element Children Iteration: `for k, v in element`

When iterating over an element, the two-variable form iterates over children with their indices.

```lambda
let el = <div; <span>"Hello">, <span>"World">>

// iterate over children with index
(for (i, child in el) {pos: i, tag: child.0})
// [{pos: 0, tag: 'span}, {pos: 1, tag: 'span}]
```

---

### Attribute Iteration: `for k, v at expr`

The `at` keyword provides access to attributes/named properties rather than children/values.

#### Syntax

```
for <value> at <expr>
for <key>, <value> at <expr>
```

#### Semantics

- `at` signals iteration over attributes/named properties
- For maps: iterates over key-value pairs (same as `in`)
- For elements: iterates over attributes (not children)

#### Map Iteration: `for k, v at map`

```lambda
let m = {x: 1, y: 2, z: 3}

// single variable: iterate over values
(for (v at m) v * 2)  // [2, 4, 6]

// two variables: iterate over key-value pairs
(for (k, v at m) k ++ "=" ++ str(v))  // ["x=1", "y=2", "z=3"]
```

#### Element Attribute Iteration: `for k, v at element`

```lambda
let el = <div class: "container", id: "main"; "Content">

// iterate over attributes
(for (k, v at el) k ++ ": " ++ v)
// ["class: container", "id: main"]

// single variable: attribute values only
(for (v at el) v)  // ["container", "main"]
```

#### Comparison: `in` vs `at`

| Expression | `in` iterates over | `at` iterates over |
|------------|-------------------|--------------------|
| Array/List | Elements (indexed) | N/A |
| Map | Key-value pairs | Key-value pairs (same) |
| Element | Children (indexed) | Attributes (key-value) |
| Range | Values in range | N/A |

---

### Positional Decomposition: `let a, b = expr`

Allows destructuring assignment based on position in a collection.

#### Syntax

```
let <name1>, <name2>, ... = <expr>
```

#### Semantics

- `<expr>` must evaluate to an array, list, map, or other ordered collection
- Variables are bound to elements by position (0-indexed)
- **Maps are ordered**: items maintain their definition order from source
- If fewer variables than elements: remaining elements are ignored
- If more variables than elements: excess variables are bound to `null`

#### Examples

```lambda
// basic positional decomposition from array
let a, b, c = [1, 2, 3]
// a = 1, b = 2, c = 3

// positional decomposition from map (ordered by definition)
let first, second = {x: 10, y: 20, z: 30}
// first = 10, second = 20 (values in definition order)

// fewer variables than elements
let x, y = [1, 2, 3, 4, 5]
// x = 1, y = 2 (rest ignored)

// more variables than elements
let p, q, r = [1, 2]
// p = 1, q = 2, r = null

// from function return
fn get_point() => [10, 20]
let x, y = get_point()
// x = 10, y = 20

// nested in expressions
(let a, b = [3, 4], a + b)  // 7

// with ranges
let start, end = 1 to 10
// start = 1, end = 10

// map decomposition by position
let name, age = {name: "Alice", age: 30}
// name = "Alice", age = 30 (values extracted by position)
```

---

### Named Decomposition: `let a, b at expr`

Allows destructuring assignment based on named properties in a map or element.

#### Syntax

```
let <name1>, <name2>, ... at <expr>
```

#### Semantics

- `<expr>` must evaluate to a map or element with named properties
- Variables are bound to properties with matching names
- If a property doesn't exist: variable is bound to `null`

#### Examples

```lambda
// map decomposition
let name, age at {name: "Alice", age: 30, city: "NYC"}
// name = "Alice", age = 30

// element attribute decomposition  
let class, id at <div class: "container", id: "main"; "Content">
// class = "container", id = "main"

// missing properties become null
let a, b, c at {a: 1, b: 2}
// a = 1, b = 2, c = null

// nested in expressions
(let x, y at {x: 3, y: 4}, x + y)  // 7

// practical example: extracting from parsed data
let title, author at input("book.json", 'json)
print(title, "by", author)
```

#### Comparison: `=` vs `at`

| Syntax             | Decomposition Type | Source                  |
| ------------------ | ------------------ | ----------------------- |
| `let a, b = expr`  | Positional         | Array, List, Map, Range |
| `let a, b at expr` | Named              | Map, Element attributes |

**Note**: Maps are ordered by definition order in Lambda. `let a, b = map` extracts values by position, while `let a, b at map` extracts values by matching variable names to keys.

---

### For-Expression Semantic Change

**Note**: This change applies only to **for-expressions** (`for (...) expr`), not **for-statements** (`for ... { }`). For-statements retain their existing behavior.

#### Current Behavior

Currently, `for (...) expr` produces a **list** with normalization applied to the results.

```lambda
// current behavior
(for (x in [1, 2, 3]) x * 2)  // produces list (2, 4, 6)
```

#### New Behavior

`for (...) expr` now produces a **spreadable array** (no normalization). This array can be:
- Spread into an outer collection
- Assigned to a variable as a standalone array

#### For-Statement: No Change

For-statements with curly braces retain their existing semantics:

```lambda
// for-statement still produces normalized list (unchanged)
for x in [1, 2, 3] {
    x * 2
}
// produces list (2, 4, 6) - same as before
```

#### Standalone For-Expression

When assigned directly to a variable, produces an array:

```lambda
let a = for (x in [1, 2, 3]) x * 2
// a = [2, 4, 6] (array, not list)
```

#### Spreading into Arrays

For-expression results spread naturally into array literals:

```lambda
[0, for (x in [1, 2, 3]) x * 2, 100]
// [0, 2, 4, 6, 100] - for-expr items spread into the array
```

#### Spreading into Lists

When spread into a list (parentheses), items are **normalized** when added:

```lambda
(0, for (x in [1, 2, 3]) x * 2, 100)
// (0, 2, 4, 6, 100) - items normalized when adding to list
```

#### Multiple Loop Variables: Flat Array

Multiple loop variables now produce a **single flat array**, not a nested array:

```lambda
// current behavior (nested)
for (x in [1, 2], y in [10, 20]) x + y
// [[11, 21], [12, 22]] - two-dimensional

// new behavior (flat)
for (x in [1, 2], y in [10, 20]) x + y
// [11, 21, 12, 22] - one flat spreadable array
```

#### Summary of Changes

| Context                        | Current Behavior      | New Behavior                 |
| ------------------------------ | --------------------- | ---------------------------- |
| `for x in arr { ... }`         | List (normalized)     | **No change** (list)         |
| `let a = for (...) expr`       | List                  | Array                        |
| `[..., for (...) expr, ...]`   | `[..., [array], ...]` | Spread into array            |
| `(..., for (...) expr, ...)`   | `(..., [array], ...)` | Spread + normalize into list |
| Multiple loop variables (expr) | Nested arrays         | Single flat array            |

#### Rationale

1. **Consistency**: Arrays are the primary ordered collection; lists are for special cases
2. **Performance**: Avoiding normalization overhead when not needed
3. **Composability**: Spreadable arrays integrate cleanly with array comprehensions
4. **Simplicity**: Flat output from nested loops is more intuitive for most use cases

#### Examples

```lambda
// building arrays with mixed content
let nums = [1, 2, 3]
let result = [
    0,
    for (n in nums) n * 10,
    for (n in nums) n * 100,
    999
]
// [0, 10, 20, 30, 100, 200, 300, 999]

// conditional spreading
let evens = [for (n in 1 to 10) if (n % 2 == 0) n]
// [2, 4, 6, 8, 10]

// nested iteration, flat result
let grid = for (x in 1 to 3, y in 1 to 3) {x: x, y: y}
// [{x:1,y:1}, {x:1,y:2}, {x:1,y:3}, {x:2,y:1}, {x:2,y:2}, {x:2,y:3}, {x:3,y:1}, {x:3,y:2}, {x:3,y:3}]
```

---

## Quick Reference

### Iteration Patterns

| Pattern | Meaning |
|---------|---------|
| `for (v in arr)` | Iterate over array/list values |
| `for (i, v in arr)` | Iterate with 0-based index |
| `for (k, v in map)` | Iterate over map key-value pairs |
| `for (i, child in elem)` | Iterate over element children with index |
| `for (v at map)` | Iterate over map values |
| `for (k, v at map)` | Iterate over map key-value pairs |
| `for (v at elem)` | Iterate over element attribute values |
| `for (k, v at elem)` | Iterate over element attribute key-value pairs |

### Decomposition Patterns

| Pattern | Meaning |
|---------|---------|
| `let a, b = [1, 2]` | Positional: a=1, b=2 |
| `let a, b = {x:1, y:2}` | Positional from map: a=1, b=2 (by definition order) |
| `let a, b at {a:1, b:2}` | Named: a=1, b=2 (by matching names) |
| `let x, y = point` | Extract from ordered collection |
| `let name, age at person` | Extract from map/element by name |

### For-Expression Output

| Context | Result Type |
|---------|-------------|
| `let x = for (...) expr` | Array |
| `[..., for (...) expr, ...]` | Items spread into array |
| `(..., for (...) expr, ...)` | Items spread + normalized into list |
