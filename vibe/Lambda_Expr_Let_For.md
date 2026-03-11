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

### Unified Two-Variable Iteration: `for k, v in container`

All container types use a single `for k, v in container` syntax. The behavior of `k` and `v` depends on the container type.

#### Syntax

```
for <key>, <value> in <container>
```

#### Container-Specific Behavior

| Container Type    | `k`                  | `v`             | Iteration Order                        |
|-------------------|----------------------|-----------------|----------------------------------------|
| Range             | 0-based index (int)  | value (int)     | start to end                           |
| Array / List      | 0-based index (int)  | element value   | sequential                             |
| Map / Object      | key (symbol)         | value           | definition order                       |
| Element           | key or index         | value           | attributes first (keyed), then children (indexed) |

For **element**, `for k, v in element` iterates over **all entries**: first all attributes (where `k` is a symbol key), then all children (where `k` is an integer index). This provides a complete view of the element's contents.

#### Examples

```lambda
// array: k = index, v = value
(for (i, v in [10, 20, 30]) [i, v])
// [[0, 10], [1, 20], [2, 30]]

// range: k = index, v = value (same as index for range)
(for (i, v in 1 to 3) [i, v])
// [[0, 1], [1, 2], [2, 3]]

// map: k = key (symbol), v = value
let m = {name: "Alice", age: 30}
(for (k, v in m) [k, v])
// [['name, "Alice"], ['age, 30]]

// element: attributes first, then children
let el = <div class: "main", id: "root"; <span>"Hello">, <span>"World">>
(for (k, v in el) [k, v])
// [['class, "main"], ['id, "root"], [0, <span>"Hello">], [1, <span>"World">]]
```

---

### Fine-Tuned For-Loop with Type-Annotated Key

The key variable can carry a type annotation to restrict iteration to specific entry kinds:

#### `for k:int, v in container` — Indexed Values Only

Iterates only over positionally-indexed entries. For elements, this means **only children** (skipping attributes).

```
for <key>:int, <value> in <container>
```

| Container Type    | Behavior                              |
|-------------------|---------------------------------------|
| Range / Array / List | Same as plain `for k, v in ...`   |
| Map / Object      | No items (maps have no indexed entries) |
| Element           | Only children (content), k = 0-based index |

```lambda
let el = <div class: "main"; <span>"Hello">, <span>"World">>

// only children
(for (i:int, child in el) [i, child])
// [[0, <span>"Hello">], [1, <span>"World">]]

// array (same as untyped)
(for (i:int, v in [10, 20]) [i, v])
// [[0, 10], [1, 20]]
```

#### `for k:symbol, v in container` — Keyed Values Only

Iterates only over keyed (named) entries. For elements, this means **only attributes** (skipping children).

```
for <key>:symbol, <value> in <container>
```

| Container Type    | Behavior                              |
|-------------------|---------------------------------------|
| Range / Array / List | No items (these have no keyed entries) |
| Map / Object      | Same as plain `for k, v in ...`    |
| Element           | Only attributes, k = key symbol    |

```lambda
let el = <div class: "main", id: "root"; <span>"Hello">>

// only attributes
(for (k:symbol, v at el) [k, v])
// [['class, "main"], ['id, "root"]]

// map (same as untyped)
(for (k:symbol, v in {x: 1, y: 2}) [k, v])
// [['x, 1], ['y, 2]]
```

#### Summary: Key Type Filter

| Syntax | Filter | Element iteration |
|--------|--------|-------------------|
| `for k, v in container` | All entries | Attributes + children |
| `for k:int, v in container` | Indexed only | Children only |
| `for k:symbol, v in container` | Keyed only | Attributes only |

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
| `for (v in map)` | Iterate over map values |
| `for (k, v in arr)` | Iterate with 0-based index |
| `for (k, v in map)` | Iterate over map key-value pairs (k = symbol) |
| `for (k, v in elem)` | Iterate over element attributes + children |
| `for (k:int, v in elem)` | Iterate over element children only (indexed) |
| `for (k:symbol, v in elem)` | Iterate over element attributes only (keyed) |
| `for (k:int, v in arr)` | Same as `for k, v in arr` |
| `for (k:symbol, v in map)` | Same as `for k, v in map` |

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
