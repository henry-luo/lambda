# Lambda Query: Type-Based Path Query Expressions

> **Status (2026-09-26):** shipped — `?`, `.?` and `[T]` are in the C parser and both tiers, and the two breaking changes below landed long ago (`^` is propagation, `**` is power). The result kind is **ruled as S8.2.4v3 (2026-09-26)**: a type query answers as XPath does — the run `T*` — while a positional selection (`[i to j]`, a mask, an index array) answers as NumPy does — an array; §4.1 records the ruling and the argument. **Implemented on both tiers 2026-09-26** (the list step, the null-drop rule, ranges under `?`, and the index-array gather); the spec's Appendix A row for S8.2.4v3 keeps the dates, the residue, and an array reading of the type query that was ruled on 2026-09-25 and reverted the next day.

## Overview

This document added a **type-based query operator `?`** to Lambda, enabling jQuery-style descendant search on elements, maps, and nested data structures. The query leverages Lambda's existing type system to express search criteria concisely.

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
   - 1.1 [Prior Art: XPath/XQuery, CSS Selectors, jQuery](#11-prior-art-xpathxquery-css-selectors-jquery)
2. [Breaking Changes](#breaking-changes)
3. [Query Syntax](#query-syntax)
4. [Semantics](#semantics)
5. [Query Scope & Traversal](#query-scope--traversal)
6. [Child-Level Query: `[T]`](#child-level-query-t)
7. [Chaining & Composition](#chaining--composition)
8. [Comparison with Existing Approaches](#comparison-with-existing-approaches)
9. [Grammar Changes](#grammar-changes)
10. [Design Decisions](#design-decisions)
11. [Implementation Plan](#implementation-plan)

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

### 1.1 Prior Art: XPath/XQuery, CSS Selectors, jQuery

Three lineages shaped `?`, `[T]` and `.?`. Each is summarized by the four
questions that matter for Lambda: *what is selected*, *along which axes*,
*how are predicates written*, and *what comes back at zero, one, and many
matches* — the last being the question §4.1 rules.

**XPath / XQuery (W3C).** The reference model for tree querying, and the
closest to Lambda's.

- *Selected*: nodes of every kind — elements, attributes, text, comments —
  and, from XPath 2.0, atomic values too. XQuery adds construction (FLWOR)
  on top of the same path language.
- *Axes*: explicit and complete — `child` (`/`), `descendant` (`//`, i.e.
  `descendant-or-self::node()/`), `attribute` (`@`), `self`, `parent`
  (`..`), `ancestor`, `following-sibling`, … Attributes are a **separate
  axis**: `//div` never returns an attribute, `//@id` never an element.
- *Predicates*: `[…]` after any step, either boolean (`//book[price > 30]`)
  or positional (`//book[1]`), evaluated with the step's node as context.
- *Result model*: XPath 1.0 returns a **node-set** — unordered, duplicate-free,
  emitted in document order. XPath 2.0+/XQuery replace it with the
  **sequence**: a flat, ordered list of items where **an item is identical
  to a singleton sequence** and `()` is the empty sequence; sequences never
  nest. So `//title` at one match *is* that title node, at none is `()`, and
  `count()` measures the sequence while `string-length()` measures the item.
  Selecting from a *set* is set-at-a-time: `//div//p` applies the second
  step to every node of the first.

**CSS Selectors (W3C Selectors 3/4) and the DOM Selectors API.** The
element-selection language every web author knows.

- *Selected*: **elements only**. A selector can test attributes
  (`[href]`, `[id="x"]`, `[class~="a"]`) and structure (`:first-child`,
  `:nth-of-type`, `:not(…)`, and in Selectors 4 `:has(…)` and `:is(…)`) but
  never returns an attribute or a text node.
- *Axes*: four combinators — descendant (` `), child (`>`), adjacent
  sibling (`+`), general sibling (`~`) — plus the implicit descendant-or-self
  scope of the element `querySelector` is called on. No parent or ancestor
  axis (until `:has`, which reverses the direction inside a predicate).
- *Predicates*: the selector *is* the predicate; there is no expression
  language, only the fixed vocabulary of simple selectors and pseudo-classes.
  Union is `,` (`h1, h2`).
- *Result model*: the API splits on cardinality rather than the value.
  `querySelector` → **the first match or `null`**; `querySelectorAll` → a
  **static `NodeList`, always** (array-like, possibly empty, never `null`).
  Two entry points, so the caller chooses the shape up front.

**jQuery.** CSS selection with a set-oriented object model on top — the
direct ancestor of Lambda's "pipe over a result set" idiom (`Lambda_Expr_Pipe.md`).

- *Selected*: elements, via CSS selectors plus jQuery extensions
  (`:visible`, `:has()`, `:contains()`, `:eq(n)` — several of which
  Selectors 4 later standardized).
- *Axes*: as methods on the set — `.find()` (descendants), `.children()`,
  `.parent()`/`.closest()` (ancestors), `.siblings()`, `.next()`/`.prev()`;
  `.filter(sel | fn)` and `.not()` narrow the set; `.end()` pops back.
- *Predicates*: selector strings, or a callback `function(index, el)` —
  the closest prior form of `~`/`~#`.
- *Result model*: a **jQuery object, always** — an array-like wrapper with
  `.length`, even at zero or one match; nothing ever unwraps. The whole API
  is built on two conventions that follow: **implicit iteration** (a setter
  or action such as `.addClass()`/`.hide()` applies to every element in the
  set) and **first-element getters** (`.attr("src")`, `.text()`, `.val()`
  read from the *first* element only). Getting one element is explicit:
  `$("img")[0]`, `.first()`, `.eq(n)`.

**What each contributed to Lambda's query, and the one place Lambda differs.**

| | XPath / XQuery | CSS / DOM Selectors | jQuery | Lambda `?` / `[T]` / `.?` |
|---|---|---|---|---|
| selection key | node test + predicate | simple selectors | selectors + callbacks | a **type** (`?int`, `?<div id: "x">`, `?{a: int}`) — no separate query vocabulary (§3) |
| descendant | `//` | ` ` (space) | `.find()` | `?` (§5.1) |
| child | `/` | `>` | `.children()` | `[T]` (§6) |
| self-or-descendant | `descendant-or-self::` | scope of the call | — | `.?` (§5.2) |
| attributes | separate `@` axis | tested, never returned | tested, never returned | **searched and returned** alongside children (§4.3, S8.1.2v2) |
| predicate language | full expression | fixed vocabulary | selector or callback | the type language itself; a constrained type `T that cond` for the rest (§10.5) |
| union | `\|` | `,` | `,` | `?(A \| B)` — the type union (S10.1.1) |
| iterate over result | set-at-a-time steps | caller loops | implicit iteration | the pipe: `html?<img> \|> ~.src` (§7.2) |
| **0 / 1 / n matches** | `()` / the item / sequence — item ≡ singleton | `null` / element (`querySelector`), `NodeList` always (`querySelectorAll`) | jQuery object always; `.length` 0/1/n | **`null` / the match / a list** — the run `T*` (§4.1, S8.2.4v3) |
| count | `count()` (not `string-length`) | `.length` | `.length` | `count()` (not `len`, S8.3.3v3) |

The result model is where the lineages disagree, and Lambda sides with XPath 2.0+: a query is an **accessor** whose singleton case is the item itself, with `()`/`null` for absence — not the CSS/jQuery "always a collection" model, which needs an unwrapping step for the common single match and a spread to place results into content (§4.1, S8.2.4v3). Steps are set-at-a-time too: a run is stepped item by item, so `html[table][tr][td]` is `//table/tr/td` (§6.5). The tell that the XPath model fits is that `count()` versus `len` in Lambda is precisely XPath's `count()` versus `string-length()`. Positional selections — `[i to j]`, a mask, an index array — are the other world and follow NumPy: always an array (§4.1).
Attribute searching is Lambda's one deliberate departure from all three:
an element's attribute values are in its key domain (S8.1.2v2), so `?string`
finds `"photo.jpg"` in `src: "photo.jpg"` as readily as a text child — XPath
would need `//@*[. = …] | //text()[…]`, CSS and jQuery cannot express it.

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

### 4.1 Return Value — the run `T*`; two subscript worlds (RULED 2026-09-26, USER; S8.2.4v3)

**Definition.** For a container `e` — a range, array, map, or element — `e[T]` iterates the content exactly as `for (x in e)` does (a map's values; an element's attribute values, then its children) and keeps each `x` that `is T` **and is not `null`**. A **list** is stepped **set-at-a-time**, as an XPath sequence is: `e[T]` on a list applies the step to each item and splices the results, so `html[table][tr][td]` reads as `//table/tr/td` at any number of tables; a scalar has no content, so a scalar item contributes nothing — `(1, "a", 2)[int]` is `null`, where the array `[1, "a", 2][int]` is `(1, 2)` (an array is a container, a list a sequence, S2.5.1v2). It answers as a subscript does: the match **itself** when there is one, `null` when there is none, a list when there are two or more — the run `T*` (S11.1.6v2), collapsing by S2.5.5v2. `e?T` is `e[T]` applied recursively, in document order (§5.3), the root excluded; `e.?T` includes it (§5.2). The result is a value, never an item-position producer.

```lambda
let imgs = html?<img>          // null | <img …> | (<img …>, <img …>, …)
count(imgs)                     // the match count: 0, 1, or n (S8.3.3v3) — len is not the count
if (imgs) …                     // the absence check reads as a null check
[*imgs, extra]                  // splice whatever there is; [*imgs] is the array form
doc[<title>]                    // the one <title>, unwrapped
html[table][tr][td]             // every cell of every table — XPath's //table/tr/td
[1, null, 2][(int | null)]      // (1, 2) — a null value never matches, whatever T admits (in an expression `int?` would be a query, so spell the type as a union)
(1 to 5)[int]                   // (1, 2, 3, 4, 5) — a range is a container too
```

**Two worlds, kept apart.** A **type key** names members and answers as an XPath node-set does: one expected match is the common case (`doc[<title>]`, `page?<h1>`) and must not need `[0]`, and a run composes as a node-set — `html[table][tr][td]` steps through every table. A **positional selection** — a range `[i to j]`, a boolean mask, an index array `[[1, 3, 5]]` — names a window and answers as NumPy does: always an array, its shape following the key (S7.1.2; S8.2.4v3 for the index array, whose out-of-range or negative position contributes `null` and whose result keeps the index array's length). Composed, `e[T][1 to 3]` slices the run — on a lone match, that value's own content — clamped, `[]` when empty, never `null`. The two models cannot be aligned without breaking one of them: an always-array type query costs the singleton case its directness, and a collapsing selection makes a window's shape depend on the data (R's `drop = TRUE`). So each keeps its own rule.

**Consequences.**

- *Counting* is `count(q)` (S8.3.3v3); `len` of a lone element match is that element's own length.
- *First of several* is `(q)[0]`; a lone match **is** the value, so `q[0]` on a lone element reads its first child. Code that must handle both counts tests `count(q)` or takes `[*q][0]`, since `[*q]` wraps a lone match as one item and is `[]` for none.
- *Chaining.* Child steps chain at any match count: `html[body][div]` is the divs of every body, `html?<table>[tr][td]` every cell of every table (§6.5). `e?T` on a list searches each item's descendants — the items themselves are not tested, as with XPath's `//` — so `html?<div>?<div>` is the divs nested in divs; `.?` includes the items.
- *Static type* is the run: the checker types a query open, since a lone match must not be unboxed as a container.
- *`that`.* `(e?T) that cond` is the whole run or `null` (S10.1.5v3); filtering the matches is `|:`.
- *No deduplication.* A list whose items nest within one another reaches a nested match once per enclosing item: with three divs nested in one another, `html?<div>?<div>` finds the innermost twice. XPath's node-sets deduplicate by node identity; Lambda values have none to deduplicate by (SI13), so the step is a plain concatenation.
- *Null-valued matches are dropped* even when `T` admits `null` (`e[(int | null)]`, `e[null]` never yield a `null` item), so an absent result is always the run of none.
- *Item position.* `[e?T, 9]` with no match is `[null, 9]`, as `[e[-1], 9]` is; with two or more matches the run splices, which `[*e?T, 9]` spells explicitly.

**Why not an array.** `list` and `array` agree at two or more items; the question is zero and one. The array reading (jQuery's, NumPy's) gives one uniform result type and a direct `len`, at the cost of unwrapping the single match (`result[0]`) and spreading to place results into content. The accessor reading makes the singleton case *expected* rather than surprising — nobody expects `e[1]` to come back wrapped — and keeps `doc[<title>]`, `html[body][div]` and `if (e?T)` direct. `|:` and `find` remain filters and return arrays (S2.5.7v4, S10.1.6): the line is a *type key* versus a *function over a source or a positional key*. Ruled 2026-09-26, after an array reading had been tried for a day and reverted.

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

### 5.1 Recursive Descendant Search: `?`

`?` performs a **depth-first recursive search** through attributes and all descendants, but does **not** include the root value itself:

```lambda
html?<p>           // all <p> at any depth
let div = <div class: "main"; <p>>
div?<div>          // null — does NOT include self
div?<p>            // <p> — finds child (a lone match is the value)
```

### 5.2 Self-Inclusive Query: `.?`

The `.?` operator includes the **value itself** in addition to everything `?` searches (attributes + all descendants):

```lambda
let div = <div class: "main"; <p>>
div.?<div>         // <div class:"main" ...> — includes self
div.?<p>           // <p> — finds child (same as ?)
42.?int            // 42 — trivial self-match
```

The key distinction:

| Operator | Includes self? | Searches |
|----------|---------------|----------|
| `?` | no | attributes + all descendants (recursively) |
| `.?` | yes | self + attributes + all descendants (recursively) |

Both operators recurse to unlimited depth. The only difference is whether the root value itself is tested against the query type.

### 5.3 Traversal Order

Results are returned in **document order** (depth-first, pre-order):

```lambda
let doc = <root;
    <a; <b> <c>>
    <d; <e>>
>
doc?element        // (<a>, <b>, <c>, <d>, <e>) — excludes <root> itself
doc.?element       // (<root>, <a>, <b>, <c>, <d>, <e>) — includes self
```

### 5.4 Traversal Targets

Lambda attributes can hold complex values (maps, arrays, elements), so the query descends into attribute values as well:

| Data Type | `?` searches | `.?` searches |
|-----------|-------------|---------------|
| Element | attributes (recursively) + children (recursively) | self + same as `?` |
| Map | values (recursively) | self + same as `?` |
| Array | items (recursively) | self + same as `?` |
| Range | its values | self + same as `?` |
| List (a run) | each item's content, recursively — the items themselves are not tested | each item, then the same as `?` |
| Scalar | (nothing — no children) | the value itself |

```lambda
// Attribute values are complex — query descends into them
let el = <widget config: {threshold: 42, items: [1, 2, 3]}; "text">
el?int             // (42, 1, 2, 3) — found inside attribute map and array
el.?int            // (42, 1, 2, 3) — same (el itself is not int)
```

### 5.5 Future: Result Limiting

In a future version, a `limit` clause may be added to stop traversal early:

```lambda
html?<img> limit 5          // return first 5 matches, stop searching
data?int limit 1            // find first int (efficient early exit)
```

This is deferred to keep the initial implementation simple.

---

## 6. Child-Level Query: `[T]`

### 6.1 Overview

While `?` performs recursive descendant search across all depths, the **child-level query** `[T]` searches only **immediate** attributes and children — one level deep:

```lambda
expr?T             // recursive: all descendants matching T
expr[T]            // child-level: direct attributes + children matching T
```

This is analogous to XPath's `/` (child axis) vs `//` (descendant axis), or CSS's `>` (child combinator) vs ` ` (descendant combinator).

### 6.2 Syntax

```
child_query := expr '[' type ']'
```

The `[T]` syntax reuses the existing index operator `expr[x]`. The interpretation depends on the type of `x` at runtime:

| Index value `x` | Interpretation |
|-----------------|----------------|
| `int` value | Positional index access (existing) |
| `string` or `symbol` value | Named field access (existing) |
| Type | Child-level query (**new**) |

Built-in type keywords (`int`, `string`, `float`, `bool`, `null`, `element`, `map`, `array`, etc.), element literals (`<tag>`), and map patterns (`{k: T}`) are syntactically unambiguous as types. For user-defined type names that look like variables, the resolution is handled at runtime in `fn_member()` — if the value resolves to a type, it performs a child-level query; otherwise, it performs normal index/named access.

### 6.3 Semantics

`expr[T]` returns the **run** of the direct attributes and children of `expr` that match type `T` (§4.1): the match, `null`, or a list.

#### On Arrays

Searches array items (one level only):

```lambda
[1, "hello", 3, "world", true][string]    // ("hello", "world")
[1, 2, 3][int]                             // (1, 2, 3)
[1, [2, 3], 4][array]                      // [2, 3] — the lone match is the inner array itself
[1, "a", null, true][(int | string)]       // (1, "a")
```

#### On Maps

Searches map **values** (one level only):

```lambda
{name: "Alice", age: 30, active: true}[string]    // "Alice"
{name: "Alice", age: 30, active: true}[int]       // 30
{x: 1, y: 2, label: "origin"}[int]                // (1, 2)
```

#### On Lists — set-at-a-time

A list is a sequence, not a container: the step applies to each item and the results splice (§4.1). A run from a previous step is a list, which is what makes chains work at any count:

```lambda
type b = <b>
(<a; <b; "1">>, <c; <b; "2">>)[b]      // (<b; "1">, <b; "2">) — the <b> child of each item
(1, "a", 2)[int]                        // null — a scalar item has no content; [1, "a", 2][int] is (1, 2)
```

#### On Elements

Searches **attribute values** and **direct children**:

```lambda
type p = <p>
type img = <img>

let el = <div class: "main" id: "content";
    <p; "hello">
    <img src: "photo.jpg">
    "some text">

el[p]            // <p; "hello"> — direct child element
el[img]          // <img src: "photo.jpg"> — direct child element
el[string]       // ("main", "content", "some text") — attr values + text children
el[element]      // (<p; "hello">, <img src: "photo.jpg">) — all child elements
```

Note: Unlike `?`, the child-level query does **not** recurse into children or attribute values. `el[string]` finds string attribute values and string direct children of `el`, but does not descend into `<p>` to find its `"hello"` text.

### 6.4 Comparison with `?`

| Feature | `expr[T]` | `expr?T` |
|---------|-----------|----------|
| Scope | Direct attributes + children only | All descendants (recursive) |
| Depth | One level | Unlimited |
| Self-inclusive variant | N/A | `.?T` |
| Analogy | XPath `/`, CSS `>` | XPath `//`, CSS ` ` |
| Return type | the run `T*` | the run `T*` |

```lambda
type a = <a>
type c = <c>

let doc = <root;
    <a; <b; <c>>>
    <d>>

doc[element]       // (<a ...>, <d>) — direct children only
doc?element        // (<a>, <b>, <c>, <d>) — all descendants

doc[a]             // <a ...> — direct child <a> only
doc?<a>            // <a ...> — same here (only one <a>)

doc[c]             // null — <c> is not a direct child
doc?<c>            // <c> — found recursively
```

### 6.5 Chaining

A run is a list and a list is stepped set-at-a-time (§4.1), so child steps chain exactly as XPath location steps do, at any number of matches:

```lambda
type table = <table>
type tr = <tr>
type td = <td>
type body = <body>
type div = <div>

html[table][tr][td]            // every cell of every table — //table/tr/td
html[body][div]?<a>            // every <a> inside the divs of every body
html?<table>[tr][td]           // all tables (recursive), then their rows, then the cells
html?<div>?<div>               // the divs nested inside divs — //div//div
```

A step over a list of scalars finds nothing, since a scalar has no content: `(data?int)[int]` is `null`. Narrowing a run by type is not a step but a filter — `q |: ~ is T`.

### 6.6 Design Notes

**No grammar changes required.** The `[T]` child-level query reuses the existing index syntax `expr[x]`. The runtime (`fn_member()`) already dispatches on the type of the index value. When `x` is a type value, it performs the child-level query instead of positional/named access. This makes the feature purely a runtime extension.

**Return type is the run `T*`, as for `?`** (S8.2.4v3, §4.1): `null`, the match, or a list. `expr[T1][T2]` steps through the run set-at-a-time (§6.5), and `expr[T] |> …` maps over the matches.

**Values only for maps.** When querying a map, only the values are tested and returned — not key-value pairs. This keeps the result uniform (a run of matched values) regardless of the container type.

---

## 7. Chaining & Composition

### 7.1 Chained Queries

Queries can be chained to narrow results:

```lambda
html?<div>?<a>                 // all <a> inside any <div>
html?<form>?<input>            // all <input> inside <form>
```

### 7.2 Query + Pipe

Queries compose naturally with Lambda's pipe operator:

```lambda
// Find all images, extract src attributes
html?<img> | ~.src

// Find all paragraphs, get their text content
html?<p> | ~[0]

// Find links, filter by href pattern
html?<a> | ~.href where ~ is string

// Count elements by type (S8.3.3v3)
count(html?<div>)

// Find and transform
html?<img> | {tag: name(~), src: ~.src}
```

### 7.3 Query + For

```lambda
// Process all matching elements
for (img in html?<img>)
    {src: img.src, alt: img.alt}

// Nested queries in comprehensions
for (form in html?<form>)
    {action: form.action, fields: form?<input> | ~.name}
```

### 7.4 Query in Conditions

```lambda
// Check if any match exists — no match is null, which is falsy (S8.2.4v3)
if (html?<img>) "has images" else "no images"

// The one expected match is the value itself; the first of several is (q)[0]
let main = html?<div id: "main">
```

---

## 8. Comparison with Existing Approaches

Syntax-level correspondence; the data models behind each column are in §1.1.

| Feature | jQuery / CSS | XPath | jq | Lambda Query |
|---------|-------------|-------|-----|-------------|
| Select by tag | `$("div")` | `//div` | — | `?<div>` |
| Select by attribute | `$("[href]")` | `//*[@href]` | — | `?<element href: string>` |
| Select by attr value | `$("[id='x']")` | `//*[@id='x']` | — | `?<element id: "x">` |
| Descendant search | `$("div p")` | `//div//p` | `.. \| .p` | `?<div>?<p>` |
| Child search | `$("div > p")` | `div/p` | `.p` | `[div][p]` (with `type div=<div>; type p=<p>`) |
| Self-inclusive | — | `self::div` | — | `.?<div>` |
| By type | — | — | `numbers` | `?int` |
| By shape | — | — | `select(.a)` | `?{a: any}` |
| Union | `$("h1, h2")` | `//h1 \| //h2` | — | `?(<h1> \| <h2>)` |

**Key advantages of Lambda's approach:**
- **Unified with the type system** — no separate query language to learn
- **Works on any data** — elements, maps, arrays, not just DOM
- **Composable** — integrates with pipes, for-expressions, and all Lambda operators
- **Statically analyzable** — type patterns are already understood by the compiler

---

## 9. Grammar Changes

### 9.1 Summary of Token Changes

| Change | Old | New | Affected Grammar Rules |
|--------|-----|------|----------------------|
| Power operator | `^` | `**` | `binary_expr` ops list |
| Error propagation | `?` | `^` | `call_expr` propagate field |
| New query operator | — | `?` | new `query_expr` rule |

### 9.2 Grammar Diff (grammar.js)

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
// S7.6.3v2: query is left-associative postfix access beside member access.
precedences: $ => [[
    $.call_expr,
    $.index_expr,
    'query_expr',
    'member',
    $.primary_expr,
    // ... lower tiers ...
], [
    // `() => x?T` is `() => (x?T)`, not `(() => x)?T`.
    'query_expr', $._expr,
]],

query_expr: $ => prec.left('query_expr', seq(
    field('object', $.primary_expr),
    field('op', choice('?', '.?')),
    field('query', $._primary_type),
)),

// Add to primary_expr choices (alongside member_expr, index_expr):
$.query_expr,
```

The two query forms share one CST node; the AST builder derives `direct` from
the `op` field. The explicit named precedence replaces the former
`[$._expr, $.query_expr]` GLR conflict and makes the arrow-body boundary
deterministic. `query_expr` takes a primary type on the right, **not** a full
type expression. This means `that` constraints are not allowed inline — they
must be wrapped in parentheses or declared as a named type (see Section 10.5).

### 9.3 AST Node

```
QueryExpr {
    object: Expr,        // the data to search
    query: PrimaryType,  // the type pattern to match (primary_type only)
    direct: bool,        // true for .? (self-inclusive query)
}
```

### 9.4 Precedence

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

## 10. Design Decisions

### 10.1 `?` returns all matches

`?` yields the run `T*` of matching values (S8.2.4v3): `null`, the one match itself, or a list. `(expr?T)[0]` is the first of several; a lone match needs no subscript, and `[*(expr?T)][0]` is the first-or-null form that works at any count:

```lambda
html?<img>           // null, the one <img>, or a list of them (S8.2.4v3)
(html?<img>)[0]      // first of several; on a lone <img> this reads its first child
[*(html?<img>)][0]   // the first match or null, at any count
count(html?<img>)    // count of matches: 0, 1, or n (S8.3.3v3)
```

### 10.2 Attributes are searched

The query searches the **entire subtree including attribute values**. Lambda attributes can hold complex values — maps, arrays, nested elements — so the query descends into all of them:

```lambda
let el = <widget data: {scores: [90, 85, 72]}; "content">
el?int             // (90, 85, 72) — found inside attribute value
```

### 10.3 Unlimited depth

The query descends into **all nested containers** with no depth limit. In the future, `?T limit N` may be introduced to cap results (see Section 5.6).

### 10.4 `?` and `?` in types do not conflict

In the type system, `T?` means `T | null` (optional). In expressions, `expr?T` means query. These are in **different syntactic contexts** (type vs. expression), so there is no ambiguity:

```lambda
type MaybeInt = int?          // type context: optional int
let found = data?int          // expr context: query for int values
```

### 10.5 Constrained queries via `that`

The `that` keyword is used for constraints in the type system (`constrained_type`), but `that` is **also** an expression operator — the single-value proviso `x that cond` (S10.1.5v3; it was the sequence filter before 2026-09-23, now `|:`). Since `?` takes `primary_type` (not `_type_expr`), a bare `that` after the query would be parsed as the expression operator applied to the query's result, creating ambiguity.

**Rules:**

| Syntax | Valid? | Meaning |
|--------|--------|---------|
| `expr?int` | ✅ | query for int |
| `expr?(int that (~ > 5))` | ✅ | query with constraint (parenthesized) |
| `type T = int that (~ > 5); expr?T` | ✅ | query via named constrained type |
| `expr?int that ~ > 5` | ❌ | ambiguous — `that` parsed as the proviso on the query result |

```lambda
// Correct: declare constrained type first, then query
type Positive = int that (~ > 0);
data?Positive

// Correct: use parenthesized type expression
data?(int that (~ > 0))

// WRONG: that binds as the expression proviso, not the type constraint
data?int that ~ > 0   // parsed as: (data?int) that (~ > 0) — the whole run, or null
```

### 10.6 Performance considerations

Recursive descendant search on large trees could be expensive. Options for future optimization:
- Lazy evaluation (yield results incrementally)
- `limit` clause for early exit
- Index/cache for repeated queries on the same tree

**Decision**: Start with eager evaluation. Profile and optimize later.

---

## 11. Implementation Plan

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

---

## 12. Future: Auto Pipe/Map with Query (KIV)

> **Status**: Keep In View — not yet implemented. Reserved for future consideration.

### Motivation

Currently, chaining member access or further expressions after a query requires an explicit pipe:

```lambda
html?<div> | ~.class          // explicit pipe to map .class over results
html?<img> | ~.src            // explicit pipe to extract src
```

An **auto pipe/map** enhancement would allow post-query expressions to implicitly map over results, yielding more XPath-like conciseness:

```lambda
html?<div>.class              // auto-map: equivalent to html?<div> | ~.class
html?<img>.src                // auto-map: equivalent to html?<img> | ~.src
```

### Proposed Semantics

When a member access (`.`), index (`[]`), or further expression follows a query, the result would be **automatically mapped** over the query results:

```lambda
expr?T.field                  // → (expr?T) | ~.field
expr?T[i]                     // → (expr?T) | ~[i]
expr?T.field.subfield         // → (expr?T) | ~.field.subfield
```

### Concerns

**1. Ambiguity with list properties**

Since `?` returns a run — a list at two or more matches — and lists and arrays have their own properties (e.g., `.length`), it becomes unclear which is intended:

```lambda
html?<div>.length             // list length (count of divs)?
                              // or: map .length over each div's content?
```

**2. Breaks referential transparency**

Extracting a query result into a variable changes behavior:

```lambda
html?<div>.class              // auto-maps → the class strings
let divs = html?<div>         // stores the run
divs.class                    // null on a list — a list has no .class property
```

The same value produces different results depending on whether it's inlined or stored. This violates a core expectation in a functional language.

**3. Not composable with existing semantics**

Lambda's current model is clean: `?` finds (returns the run), `|` transforms. Two distinct, composable operations. Merging them into one overloaded operator reduces orthogonality.

### Decision

**Deferred.** The explicit pipe syntax `html?<div> | ~.class` is concise enough and avoids all ambiguity. If user demand or real-world usage patterns strongly favor the auto-map form, it can be revisited — potentially with a distinct operator or syntax to avoid the concerns above.
