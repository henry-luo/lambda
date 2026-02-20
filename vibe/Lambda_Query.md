# Lambda Query: Type-Based Path Query Expressions

## Overview

This proposal adds a **type-based query operator `?`** to Lambda, enabling jQuery-style descendant search on elements, maps, and nested data structures. The query leverages Lambda's existing type system to express search criteria concisely.

```lambda
html?<img>                        // find all <img> elements in html tree
doc?{author: string}              // find nodes with author attribute
data?(int | string)               // find all int or string values
```

This requires two **breaking syntax changes** to free the `?` operator for query use:
1. Error propagation: `func()?` → `func()^`
2. Power operator: `a ^ b` → `a ** b`

---

## Table of Contents

1. [Motivation](#motivation)
2. [Breaking Changes](#breaking-changes)
3. [Query Syntax](#query-syntax)
4. [Semantics](#semantics)
5. [Query Scope & Traversal](#query-scope--traversal)
6. [Chaining & Composition](#chaining--composition)
7. [Comparison with Existing Approaches](#comparison-with-existing-approaches)
8. [Grammar Changes](#grammar-changes)
9. [Open Questions](#open-questions)
10. [Implementation Plan](#implementation-plan)

---

## 1. Motivation

Lambda is designed for data processing and document transformation. It already has rich support for element trees (HTML, XML, Markdown → Lambda elements) and nested maps/arrays. However, **querying deep structures** currently requires manual recursion or verbose `for` comprehensions:

```lambda
// Current: find all <img> in an HTML tree (manual traversal)
fn find_img(el) {
    if (el is element and name(el) == 'img) [el]
    else if (el is element) for (child in el) *find_img(child)
    else []
}

// Proposed: one expression
html?<img>
```

Key use cases:
- **HTML/XML processing**: Find elements by tag, class, attributes — the jQuery use case
- **JSON/data querying**: Extract values by type or shape from nested JSON — the jq/JSONPath use case
- **Schema validation**: Find all nodes matching a type pattern
- **Document transformation**: Select and transform matching subtrees

---

## 2. Breaking Changes

### 2.1 Error Propagation: `?` → `^`

**Current syntax:**
```lambda
fn compute(x: int) int^ {
    let a = parse(input)?      // propagate error
    let b = divide(a, x)?      // propagate error
    a + b
}
fun()?                          // propagate error, discard value
```

**New syntax:**
```lambda
fn compute(x: int) int^ {
    let a = parse(input)^      // propagate error
    let b = divide(a, x)^      // propagate error
    a + b
}
fun()^                          // propagate error, discard value
```

**Rationale:** The `^` symbol already denotes error types in Lambda (`int^`, `int ^ Error`). Using `^` for propagation creates a unified "error" sigil: `^` = "error-related operation".

| Context | Current | Proposed |
|---------|---------|----------|
| Error return type | `fn f() int^` | `fn f() int^` (unchanged) |
| Error type union | `int ^ Error` | `int ^ Error` (unchanged) |
| Error destructure | `let a^err = expr` | `let a^err = expr` (unchanged) |
| Error propagation | `expr?` | `expr^` |

**Migration**: Mechanical replacement of `)?` → `)^` in all call sites. The `^` is only valid after `)` in call expressions (same position as current `?`), so there is no ambiguity with the error union type `T^` which appears in type contexts.

### 2.2 Power Operator: `^` → `**`

**Current syntax:**
```lambda
2 ^ 3              // 8
x ^ 2              // x squared
[1,2,3] | ~ ^ 2   // [1,4,9]
```

**New syntax:**
```lambda
2 ** 3              // 8
x ** 2              // x squared
[1,2,3] | ~ ** 2   // [1,4,9]
```

**Rationale:** `**` is the power operator in Python, JavaScript, and many modern languages. This frees `^` for exclusive use as the "error" sigil. The current overloading of `^` (power *and* error type) has already been a source of grammar conflicts.

| Context | Current | Proposed |
|---------|---------|----------|
| Power expression | `a ^ b` | `a ** b` |
| Unary caret (error check) | `^err` | `^err` (unchanged) |
| Error union type | `T^` / `T ^ E` | `T^` / `T ^ E` (unchanged) |

**Migration**: Mechanical replacement of `^` → `**` in arithmetic contexts. The grammar distinguishes these contexts (binary expression vs. type expression) so no ambiguity arises.

---

## 3. Query Syntax

### 3.1 Grammar Rule

```
query_expr := expr '?' primary_type
```

The query operator `?` is a **postfix binary operator** that takes an expression on the left and a `primary_type` on the right. It has the same precedence level as member access (`.`) and index access (`[]`).

### 3.2 Query Forms

| Form | Example | Meaning |
|------|---------|---------|
| Type query | `data?int` | Find all `int` values |
| Element query | `html?<img>` | Find all `<img>` elements |
| Map query | `data?{name: string}` | Find maps with matching shape |
| Literal query | `data?123` | Find the literal value 123 |
| Union query | `data?(int \| string)` | Find `int` or `string` values |
| Array query | `data?[int]` | Find arrays of int |
| Range query | `data?(1 to 100)` | Find values in range |

### 3.3 Element-Specific Queries

Since HTML/XML processing is a primary use case, element queries get special attention:

```lambda
// By tag name
html?<div>                     // all <div> elements
html?<img>                     // all <img> elements

// By tag with attribute constraints
html?<div class: string>       // <div> with class attribute
html?<img src: string>         // <img> with src attribute
html?<a href: string>          // <a> with href attribute

// By attribute value
html?<div id: "main">         // <div id="main">
html?<input type: "text">     // <input type="text">
```

### 3.4 Map Queries (Structural Matching)

```lambda
// Find maps with specific fields
data?{name: string}            // any map with string 'name' field
data?{age: int}                // any map with int 'age' field
data?{x: int, y: int}         // maps with both x and y int fields

// Find maps with specific values
data?{status: "active"}       // maps where status == "active"
data?{score: (80 to 100)}     // maps where score is in 80..100
```

---

## 4. Semantics

### 4.1 Return Value

A query **always returns a list** of matching values (possibly empty):

```lambda
let imgs = html?<img>          // list of matching <img> elements
len(imgs)                       // count of results
imgs[0]                         // first match (or null if none)
```

### 4.2 Match Rules

The query `expr?T` finds all values `v` **within** `expr` where `v is T` holds. For compound types:

| Query Type | Match Condition |
|------------|----------------|
| `?int` | `v is int` |
| `?<tag>` | `v` is an element with tag name `tag` |
| `?<tag attr: T>` | `v` is `<tag>` AND `v.attr is T` |
| `?{a: T}` | `v` is a map AND `v.a is T` |
| `?{a: 123}` | `v` is a map AND `v.a == 123` |
| `?(T1 \| T2)` | `v is T1` or `v is T2` |
| `?"hello"` | `v == "hello"` |

### 4.3 Attribute vs. Descendant Matching

The query searches **both attributes and descendants**:

```lambda
let page = <html;
    <head; <title; "Hello">>
    <body;
        <div class: "main";
            <img src: "photo.jpg">
            <p; "text">
        >
    >
>

page?<img>         // finds the <img> deep inside <body>
page?string        // finds "Hello", "photo.jpg", "text", "main"
page?<div>         // finds the <div class:"main"> element
```

---

## 5. Query Scope & Traversal

### 5.1 Default: Recursive Descendant Search

By default, `?` performs a **depth-first recursive search** through the entire subtree, similar to jQuery's `$("selector")` or XPath's `//`:

```lambda
html?<p>           // all <p> at any depth (like XPath //p)
```

### 5.2 Self + Direct Level: `.?`

The `.?` operator searches the **value itself**, its **attributes**, and its **immediate content/children** — but does **not** recurse into descendants:

```lambda
el.?int             // int values in el's attributes and direct children
body.?<p>           // only <p> directly under body (not nested deeper)
map.?string         // string values at the top level of the map
```

The key distinction:

| Operator | Searches | Recurses? |
|----------|----------|----------|
| `?` | entire subtree (descendants) | yes, unlimited depth |
| `.?` | self + attributes + direct content | no |

This is analogous to XPath `/` (single step) vs. `//` (descendant-or-self).

### 5.3 Self-Inclusive

The `?` operator **includes the root value itself** in the search. If `expr` itself matches `T`, it appears in the results:

```lambda
let div = <div class: "main"; <p>>
div?<div>          // (<div class:"main" ...>) — includes self
div?<p>            // (<p>)
42?int             // (42) — trivial self-match
```

### 5.4 Traversal Order

Results are returned in **document order** (depth-first, pre-order):

```lambda
let doc = <root;
    <a; <b> <c>>
    <d; <e>>
>
doc?element        // (<root>, <a>, <b>, <c>, <d>, <e>)
```

### 5.5 Traversal Targets

Lambda attributes can hold complex values (maps, arrays, elements), so the query descends into attribute values as well:

| Data Type | `?` searches |
|-----------|-------------|
| Element | self + attributes (recursively) + children (recursively) |
| Map | self + values (recursively) |
| Array / List | self + items (recursively) |
| Scalar | the value itself (base case) |

```lambda
// Attribute values are complex — query descends into them
let el = <widget config: {threshold: 42, items: [1, 2, 3]}; "text">
el?int             // (42, 1, 2, 3) — found inside attribute map and array
el.?int            // (42, 1, 2, 3) — same here: .? searches attrs + direct content
```

### 5.6 Future: Result Limiting

In a future version, a `limit` clause may be added to stop traversal early:

```lambda
html?<img> limit 5          // return first 5 matches, stop searching
data?int limit 1            // find first int (efficient early exit)
```

This is deferred to keep the initial implementation simple.

---

## 6. Chaining & Composition

### 6.1 Chained Queries

Queries can be chained to narrow results:

```lambda
html?<div>?<a>                 // all <a> inside any <div>
html?<form>?<input>            // all <input> inside <form>
```

### 6.2 Query + Pipe

Queries compose naturally with Lambda's pipe operator:

```lambda
// Find all images, extract src attributes
html?<img> | ~.src

// Find all paragraphs, get their text content
html?<p> | ~[0]

// Find links, filter by href pattern
html?<a> | ~.href where ~ is string

// Count elements by type
len(html?<div>)

// Find and transform
html?<img> | {tag: name(~), src: ~.src}
```

### 6.3 Query + For

```lambda
// Process all matching elements
for (img in html?<img>)
    {src: img.src, alt: img.alt}

// Nested queries in comprehensions
for (form in html?<form>)
    {action: form.action, fields: form?<input> | ~.name}
```

### 6.4 Query in Conditions

```lambda
// Check if any match exists
if (len(html?<img>) > 0) "has images" else "no images"

// First match or default
let main = (html?<div id: "main">)[0]
```

---

## 7. Comparison with Existing Approaches

| Feature | jQuery / CSS | XPath | jq | Lambda Query |
|---------|-------------|-------|-----|-------------|
| Select by tag | `$("div")` | `//div` | — | `?<div>` |
| Select by attribute | `$("[href]")` | `//*[@href]` | — | `?<element href: string>` |
| Select by attr value | `$("[id='x']")` | `//*[@id='x']` | — | `?<element id: "x">` |
| Descendant search | `$("div p")` | `//div//p` | `.. \| .p` | `?<div>?<p>` |
| Self + direct | `$("div > p")` | `/div/p` | `.p` | `.?<p>` |
| By type | — | — | `numbers` | `?int` |
| By shape | — | — | `select(.a)` | `?{a: any}` |
| Union | `$("h1, h2")` | `//h1 \| //h2` | — | `?(<h1> \| <h2>)` |

**Key advantages of Lambda's approach:**
- **Unified with the type system** — no separate query language to learn
- **Works on any data** — elements, maps, arrays, not just DOM
- **Composable** — integrates with pipes, for-expressions, and all Lambda operators
- **Statically analyzable** — type patterns are already understood by the compiler

---

## 8. Grammar Changes

### 8.1 Summary of Token Changes

| Change | Old | New | Affected Grammar Rules |
|--------|-----|------|----------------------|
| Power operator | `^` | `**` | `binary_expr` ops list |
| Error propagation | `?` | `^` | `call_expr` propagate field |
| New query operator | — | `?` | new `query_expr` rule |

### 8.2 Grammar Diff (grammar.js)

#### Power operator `^` → `**`

```javascript
// In binary_expr ops:
// Before:
['^', 'binary_pow', 'right'],
// After:
['**', 'binary_pow', 'right'],
```

#### Error propagation `?` → `^`

```javascript
// In call_expr:
// Before:
optional(field('propagate', '?')),
// After:
optional(field('propagate', '^')),
```

#### New query rule

```javascript
// query_expr at same precedence level as member_expr (inside primary_expr):
query_expr: $ => seq(
    field('object', $.primary_expr),
    '?',
    field('query', $.primary_type),
),

// Add to primary_expr choices (alongside member_expr, index_expr):
$.query_expr,

// Direct-level query variant: .?
direct_query_expr: $ => seq(
    field('object', $.primary_expr),
    '.?',
    field('query', $.primary_type),
),
```

Note: `query_expr` takes `primary_type` on the right, **not** `_type_expr`. This means `that` constraints are not allowed inline — they must be wrapped in parentheses or declared as a named type (see Section 9.5).

### 8.3 AST Node

```
QueryExpr {
    object: Expr,        // the data to search
    query: PrimaryType,  // the type pattern to match (primary_type only)
    direct: bool,        // true for .? (self + attrs/content only)
}
```

### 8.4 Precedence

The `?` query operator binds at the **same level as `.`** (member access) — both are part of `primary_expr`:

```
1. () [] . .? ?   — grouping, index, member, query  ← same level
2. - + not        — unary
3. **             — power (was ^)
4. * / div %      — multiplicative
5. + -            — additive
6. < <= > >=      — relational
7. == !=          — equality
8. and            — logical AND
9. or            — logical OR
10. to            — range
11. is in         — type/membership
12. | where       — pipe and filter
```

Since `?` and `.` are at the same level, chaining works naturally:

```lambda
html?<div>.class           // query, then member access on each result
html?<div>?<a>             // chained query
html?<img> | ~.src         // query binds tighter than pipe ✓
```

---

## 9. Design Decisions

### 9.1 `?` returns all matches

`?` always returns a **list** of all matching values (possibly empty). Use `(expr?T)[0]` for first match:

```lambda
html?<img>         // list of all <img> elements
(html?<img>)[0]    // first <img>, or null if none
len(html?<img>)    // count of matches
```

### 9.2 Attributes are searched

The query searches the **entire subtree including attribute values**. Lambda attributes can hold complex values — maps, arrays, nested elements — so the query descends into all of them:

```lambda
let el = <widget data: {scores: [90, 85, 72]}; "content">
el?int             // (90, 85, 72) — found inside attribute value
```

### 9.3 Unlimited depth

The query descends into **all nested containers** with no depth limit. In the future, `?T limit N` may be introduced to cap results (see Section 5.6).

### 9.4 `?` and `?` in types do not conflict

In the type system, `T?` means `T | null` (optional). In expressions, `expr?T` means query. These are in **different syntactic contexts** (type vs. expression), so there is no ambiguity:

```lambda
type MaybeInt = int?          // type context: optional int
let found = data?int          // expr context: query for int values
```

### 9.5 Constrained queries via `that`

The `that` keyword is used for constraints in the type system (`constrained_type`), but `that` is **also** the pipe filter operator in expression context. Since `?` takes `primary_type` (not `_type_expr`), a bare `that` after the query would be parsed as a pipe filter, creating ambiguity.

**Rules:**

| Syntax | Valid? | Meaning |
|--------|--------|---------|
| `expr?int` | ✅ | query for int |
| `expr?(int that (~ > 5))` | ✅ | query with constraint (parenthesized) |
| `type T = int that (~ > 5); expr?T` | ✅ | query via named constrained type |
| `expr?int that ~ > 5` | ❌ | ambiguous — `that` parsed as pipe filter |

```lambda
// Correct: declare constrained type first, then query
type Positive = int that (~ > 0);
data?Positive

// Correct: use parenthesized type expression
data?(int that (~ > 0))

// WRONG: that binds as pipe filter, not type constraint
data?int that ~ > 0   // parsed as: (data?int) that (~ > 0)
```

### 9.6 Performance considerations

Recursive descendant search on large trees could be expensive. Options for future optimization:
- Lazy evaluation (yield results incrementally)
- `limit` clause for early exit
- Index/cache for repeated queries on the same tree

**Decision**: Start with eager evaluation. Profile and optimize later.

---

## 10. Implementation Plan

### Phase 1: Breaking Changes (grammar + transpiler + runtime)
1. Change power operator `^` → `**` in grammar, AST builder, transpiler, all tests
2. Change error propagation `?` → `^` in grammar, AST builder, transpiler, all tests
3. Update all documentation and test scripts

### Phase 2: Query Operator
1. Add `query_expr` and `direct_query_expr` to grammar
2. Add `QueryExpr` AST node
3. Implement recursive type-matching traversal in runtime
4. Implement `query_expr` in transpiler (C code generation)
5. Implement `query_expr` in MIR JIT

### Phase 3: Testing & Polish
1. Unit tests: scalar queries, element queries, map queries, chained queries
2. Integration tests: HTML processing, JSON processing
3. Performance benchmarks on large document trees
4. Documentation updates

---

## Appendix: Complete Example

```lambda
// Load and query an HTML document
let html = input("page.html", 'html')

// Find all images
let images = html?<img>
print("Found " ++ len(images) ++ " images")

// Extract all image sources  
let srcs = images | ~.src

// Find all links with href
let links = html?<a href: string>

// Build a site map
let sitemap = for (link in links)
    {url: link.href, text: link[0]}

// Find all headings (h1 through h6)
let headings = html?(<h1> | <h2> | <h3> | <h4> | <h5> | <h6>)

// Find form inputs and their types
let inputs = html?<form>?<input>
let field_types = inputs | {name: ~.name, type: ~.type}

// Find all numeric data in a JSON structure
let data = input("data.json", 'json')
let numbers = data?int
let stats = {count: len(numbers), sum: sum(numbers), avg: avg(numbers)}

format(stats, 'json')
```
