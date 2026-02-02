# Lambda Set-Oriented Pipe Operator Proposal

## Overview

This proposal introduces a **set-oriented pipe operator** (`|`) and a **current item reference** (`~`) to Lambda Script, enabling declarative, auto-mapping data transformation pipelines consistent with Lambda's high-level, set-oriented design philosophy.

## Rejected Alternative: Scalar Pipeline

We explicitly **reject** the scalar pipeline design (as seen in Elixir, F#, and JavaScript proposals) where the pipe simply passes a single value to the next expression. 

**Why scalar pipes don't fit Lambda:**

```lambda
// Scalar pipe (REJECTED) — requires explicit map calls
[1, 2, 3] | map(x => x * 2) | filter(x => x > 2) | sum()

// This is just function composition with extra syntax
// It doesn't leverage Lambda's set-oriented nature
```

Lambda already has:
- **Vector arithmetic**: `[1, 2, 3] * 2` → `[2, 4, 6]`
- **Implicit broadcasting**: Operations naturally extend over collections
- **For expressions**: `for (x in data) expr` for explicit iteration

A scalar pipe would be redundant and inconsistent with Lambda's design as a **high-level, set-oriented language** for data processing.

## Design Philosophy: Set-Oriented Pipes

Lambda's pipe operator should follow the same philosophy as its arithmetic operators: **automatically operate over collections**.

Just as `[1, 2, 3] + 1` produces `[2, 3, 4]` without explicit mapping, the pipe operator should automatically iterate when the context is a collection.

## Design Inspiration

### Unix Shell Pipe (Primary Reference)

The Unix shell pipe is the **grandfather of all stream-oriented pipelines** and the primary inspiration for Lambda's design:

```bash
cat users.txt | grep "active" | sort | uniq | wc -l
```

**Unix pipe characteristics:**
- Data flows as a **stream of lines** (text records)
- Each command processes **line-by-line** implicitly
- Current line is implicit (stdin) — no explicit placeholder needed
- Results stream to the next command
- **Aggregators** like `sort`, `uniq`, `wc` collect all input before producing output

**Lambda's adaptation:**

| Unix Shell           | Lambda                  | Unit of Processing     |
| -------------------- | ----------------------- | ---------------------- |
| Lines of text        | Collection of items     | Data granularity       |
| `$0`, `$1` (awk)     | `~`                     | Current item reference |
| `grep pattern`       | `where condition`       | Filter                 |
| `awk '{print $1}'`   | `\| ~.field`           | Transform/project      |
| `sort`, `uniq`, `wc` | `sort`, `unique`, `len` | Aggregators            |

```bash
# Unix: process lines
cat data.txt | awk '{ print $1 }' | sort | uniq

# Lambda: process items
data | ~.name | sort | unique
```

The key insight: **Unix pipes process streams of text lines; Lambda pipes process collections of structured items.**

### XPath `/` Operator

XPath is highly relevant for Lambda because both are **markup-oriented** languages. XPath's `/` operator is inherently set-oriented:

```xpath
//book/author/name
```

- Each step operates on a **node set**
- Results are automatically collected into a new node set
- The "current node" (`.`) refers to each item being processed

```xpath
//book[./price > 30]/title
```

This maps naturally to Lambda's use case of processing structured documents (JSON, XML, HTML, Markdown).

### PowerShell Object Pipeline

PowerShell pipes pass **objects** through, with cmdlets auto-iterating over collections:

```powershell
Get-Process | Where-Object { $_.CPU -gt 100 } | ForEach-Object { $_.Name }
```

- **`$_`** — Current item in the pipeline iteration
- Pipeline implicitly iterates; each cmdlet processes items one-by-one
- Results are collected automatically

### jq JSON Stream Processing

jq treats pipes as **stream transformers**:

```jq
.users[] | select(.age > 18) | .name
```

- **`.`** — Current input item
- The `[]` unwraps arrays into streams
- Pipes process each item, collecting results

### LINQ (C#) Method Chaining

While syntactically different, LINQ's deferred execution model is set-oriented:

```csharp
users.Where(u => u.Age > 18).Select(u => u.Name).ToList()
```

### Raku (Perl 6) Hyper Operators

Raku has explicit "hyper" versions of operators for set operations:

```raku
@numbers».sqrt    # Apply sqrt to each element
@a »+« @b         # Element-wise addition
```

## Proposed Design for Lambda

### Syntax

```
<expression> | <expression-with-~>
```

- **`|`** — Set-oriented pipe operator
- **`~`** — Current item reference (the element being iterated)

### Core Semantics: Auto-Mapping

The pipe operator **automatically maps** over collections:

```lambda
// When left side is a collection, ~ binds to each item
[1, 2, 3] | ~ * 2
// Equivalent to: for (x in [1, 2, 3]) x * 2
// Result: [2, 4, 6]
```

```lambda
// When left side is scalar, ~ binds to the whole value
42 | ~ * 2
// Result: 84
```

### Formal Semantics

```
evaluate(A | B):
    context = evaluate(A)
    if context is collection:
        return [evaluate(B) with ~ = item for item in context]
    else:
        return evaluate(B) with ~ = context
```

**Collections** include: `Array`, `List`, `Range`, `Element` (children), node sets.

**Scalars** include: `int`, `float`, `string`, `Map`, single `Element`, etc.

### Behavior Summary

| Left Side                  | `~` Binds To     | `~#` Binds To | Result                        |
| -------------------------- | ---------------- | ------------- | ----------------------------- |
| `[a, b, c]` (array)        | Each element     | Index (0, 1, 2) | Array of transformed elements |
| `(a, b, c)` (list)         | Each element     | Index (0, 1, 2) | List of transformed elements  |
| `1 to 10` (range)          | Each number      | Position (0-9) | Array of results              |
| `<div> children... </div>` | Each child       | Index (0, 1, ...) | List of transformed children  |
| `{a: 1, b: 2}` (map)       | Each value (1, 2) | Key ('a', 'b') | Collection of transformed results |
| `42` (scalar)              | The value itself | N/A | Single transformed value      |

### Key/Index Access with `~#`

The `~#` token provides access to the **key** (for maps) or **index** (for arrays/lists) of the current item:

```lambda
// Arrays — ~# is index (0-based)
['a', 'b', 'c'] | {index: ~#, value: ~}
// [{index: 0, value: 'a'}, {index: 1, value: 'b'}, {index: 2, value: 'c'}]

// Lists — ~# is index
(10, 20, 30) | ~ * (~# + 1)
// [10*1, 20*2, 30*3] → [10, 40, 90]

// Maps — iterates over key-value pairs, ~ is value, ~# is key
{a: 1, b: 2} | ~
// [1, 2] — values

{a: 1, b: 2} | ~#
// ['a', 'b'] — keys

{a: 1, b: 2} | {key: ~#, value: ~}
// [{key: 'a', value: 1}, {key: 'b', value: 2}]

// Map example — transform key-value pairs
{name: "Alice", age: 30} | {field: ~#, val: ~}
// [{field: 'name', val: "Alice"}, {field: 'age', val: 30}]

// Ranges — ~# is position
5 to 8 | {pos: ~#, val: ~}
// [{pos: 0, val: 5}, {pos: 1, val: 6}, {pos: 2, val: 7}, {pos: 3, val: 8}]
```

### Why `~#`? Alternatives Considered

| Option | Maps | Arrays/Lists | Pros | Cons |
|--------|------|--------------|------|------|
| **`~#`** ✓ | key | index | Short, neutral, universal | Slightly cryptic at first |
| `~key` / `~index` | `~key` | `~index` | Semantically precise | Two keywords to remember |
| `~key` everywhere | `~key` | `~key` | Consistent | "key" feels wrong for numeric index |
| `~i` | `~i` | `~i` | Very short | "i" feels index-specific, odd for maps |

**Precedents in other languages:**

| Language | Key/Index Access | Notes |
|----------|------------------|-------|
| Raku | `.kv` method | Treats array indices as "keys" — no distinction |
| PHP | Numeric array "keys" | Arrays use numeric "keys" uniformly |
| Python | `enumerate()` | Separate function, returns (index, value) tuples |
| XPath | `position()` | Neutral function name |
| jq | `to_entries` | Converts to `{key, value}` objects |
| PowerShell | Manual `$i++` | No built-in index access in pipeline |

**Decision: `~#`**
1. **Neutral** — doesn't favor "key" or "index" terminology
2. **Short** — minimal visual noise in pipelines  
3. **Intuitive** — `#` universally suggests "number/position"
4. **Consistent** — one symbol for all collection types
5. **Distinct** — clearly part of the `~` family, won't conflict with property access

### Why `|` for Pipe?

| Alternative | Issue |
|-------------|-------|
| `/` | Conflicts with division |
| `\|>` | Two characters; less clean |
| `\|\|` | Conflicts with logical OR in C/Java/JavaScript tradition |
| `>>` | Could conflict with bitwise operators |

The `|` operator:
- **Unix heritage** — The original pipe symbol from shell
- **Single character** — Minimal visual noise, maximum readability
- **Intuitive** — Universally recognized as "pipe" or "flow"

### Type Pattern Restrictions

Using `|` for pipe creates a conflict with the type union operator `|`. We resolve this by **restricting where full type patterns can appear**:

**Full type patterns allowed in type contexts:**
```lambda
type Result = int | error           // type declaration
let x: int | string = value         // type annotation
fn process(x: int | null) int | error { ... }  // function signature
for (item: Element | Text in nodes) // for binding
```

**Restricted type patterns in `is` expressions:**
```lambda
// Allowed in 'is' expressions (simple types only):
x is int            // simple type
x is string         // simple type
x is MyType         // type reference

// NOT allowed in 'is' expressions:
x is int | string   // ✗ '|' is pipe operator
x is !null          // ✗ '!' reserved for future
x is string?        // ✗ '?' reserved for future
x is T & U          // ✗ intersection not in expr
x is T*             // ✗ ambiguous with multiply
x is T+             // ✗ ambiguous with add
```

**Workarounds for complex type checks:**
```lambda
// Instead of: x is int | string
(x is int) or (x is string)

// Or define a type alias:
type IntOrString = int | string;
x is IntOrString
```

**Grammar distinction:**
- `type_expr` = simple type (`primary_type` only) — used in `is` expressions
- `type_pattern` = full pattern with operators — used in type declarations, annotations

This trade-off is worthwhile because:
1. **Pipe is more common** — Used constantly in data processing
2. **Complex type checks are rare** — Simple `is T` covers 95% of cases
3. **Type aliases are cleaner** — Naming complex types improves readability
4. **Reserves `?`** — For future use (optional chaining, null coalescing, etc.)

### Why `~` for Current Item?

| Alternative | Issue |
|-------------|-------|
| `.` | Used for member access (`obj.field`) |
| `$_` | Verbose; Perl-ish |
| `_` | Often used for ignored/wildcard values |
| `it` | Keyword-like; could conflict with identifiers |
| `@` | Could conflict with decorators (future) |

The `~` symbol:
- Lightweight single character
- Evokes "approximately here" / "current position"
- Familiar from shell (`~` for home = "current user's place")
- Not used elsewhere in Lambda syntax

## Examples

### Basic Collection Transformation

```lambda
// Double each number
[1, 2, 3, 4, 5] | ~ * 2
// Result: [2, 4, 6, 8, 10]

// Same as vector arithmetic (but pipe allows complex expressions)
[1, 2, 3, 4, 5] * 2
// Result: [2, 4, 6, 8, 10]
```

### Chained Transformations

```lambda
[1, 2, 3, 4, 5]
    | ~ ^ 2           // square each: [1, 4, 9, 16, 25]
    | ~ + 1           // add 1: [2, 5, 10, 17, 26]
    | if (~ > 10) ~ else null  // filter: [null, null, null, 17, 26]
    | filter((x) => x != null, ~)  // remove nulls
// Result: [17, 26]
```

### XPath-like Document Navigation

```lambda
// Given HTML document
let doc = input("page.html", 'html')

// Select all links (XPath: //a)
doc | if (~ is Element and ~.tag == 'a') ~ else null

// Get href attributes from all links (XPath: //a/@href)
doc | ~.children | if (~.tag == 'a') ~.href else null

// More fluent with element pattern matching
doc | <a href: ~href> | ~href
```

### Data Processing Pipeline

```lambda
input("users.json", 'json').users
    | {name: ~.name, age: ~.age}     // project fields
    | if (~.age >= 18) ~ else null    // filter adults
    | ~.name                          // extract names
    | string.upper(~)                 // uppercase
// Result: ["ALICE", "BOB", "CHARLIE", ...]
```

### Nested Collection Processing

```lambda
// Process nested data: users with multiple orders
input("data.json", 'json').users
    | {
        user: ~.name,
        total: sum(~.orders | ~.amount)  // nested pipe
    }
```

### Element/HTML Construction

```lambda
// Transform markdown sections into HTML cards
input("content.md", 'markdown').sections
    | <div class: "card";
           <h2 ~.title>
           <p ~.body>
       >
    | format(~, 'html')
```

### Working with Ranges

```lambda
// Generate multiplication table row
1 to 10 | ~ * 7
// Result: [7, 14, 21, 28, 35, 42, 49, 56, 63, 70]
```

### Conditional Mapping

```lambda
// Different transformation based on type
mixed_data | if (~ is int) ~ * 2
              else if (~ is string) string.upper(~)
              else ~
```

### Aggregation After Mapping

Aggregators (`sort`, `unique`, `sum`, `avg`, `len`, etc.) naturally collect all items before producing output — just like Unix commands `sort`, `uniq`, `wc`:

```lambda
// Unix equivalent: cat data.txt | awk '{print $1}' | sort | uniq
data | ~.name | sort | unique

// Sum of squares
[1, 2, 3, 4, 5] | ~ ^ 2 | sum
// [1, 4, 9, 16, 25] → 55

// Average after filtering
scores where ~ >= 60 | avg
// Keep passing scores, compute average
```

**Aggregator behavior:**
- Aggregators receive the **entire collection** from the previous pipe
- They produce a **single value** (or sorted/filtered collection)
- No special syntax needed — aggregators are just functions that expect collections

```lambda
// Chain of transformations ending with aggregation
input("sales.json", 'json').transactions
    | ~.amount                    // project: [100, 200, 150, ...]
    where ~ > 100                  // filter:  [200, 150, ...]
    | sum                         // aggregate: 350
```

**Common aggregators:**

| Aggregator | Description | Unix Equivalent |
|------------|-------------|-----------------|
| `sort` | Sort collection | `sort` |
| `unique` | Remove duplicates | `uniq` |
| `reverse` | Reverse order | `tac` |
| `sum` | Sum all values | `awk '{s+=$1} END {print s}'` |
| `avg` | Average | — |
| `len` | Count items | `wc -l` |
| `min`, `max` | Extrema | — |
| `first`, `last` | First/last item | `head -1`, `tail -1` |

### Map Pipe vs Aggregated Pipe

The pipe operator has two modes, determined by whether `~` appears in the right-hand expression:

#### Map Pipe (with `~`)

When `~` is used, the pipe **auto-maps** over the collection:

```lambda
// ~ used → iterate over items
data | ~ * 2           // [1,2,3] → [2,4,6]
data | ~.name          // [{name:"a"}, {name:"b"}] → ["a", "b"]

// Equivalent to for-expression
[1, 2, 3] | ~ * 2
// Same as: for (~ in [1, 2, 3]) ~ * 2
```

#### Aggregated Pipe (without `~`)

When `~` is **not** used, the pipe passes the **entire left side** as the **first argument** to the right side:

```lambda
// No ~ → pass whole collection to function
data | sum             // sum(data) → 6
data | sort            // sort(data) → [1, 2, 3]
data | len             // len(data) → 3
data | take(3)         // take(data, 3) → first 3 items
data | slice(1, 3)     // slice(data, 1, 3)
```

**Rules for aggregated pipe:**
1. Right side must be a function (or partial application)
2. Left side is passed as the **first argument**
3. Additional arguments can be provided: `data | take(3)` → `take(data, 3)`

```lambda
// Chain of transformations ending with aggregation
[1, 2, 3, 4, 5]
    | ~ ^ 2            // map: [1, 4, 9, 16, 25]
    | sum              // aggregate: sum([1, 4, 9, 16, 25]) → 55

// Multiple aggregators
data | sort | take(5) | reverse
// sort(data) → take(_, 5) → reverse(_)
```

This mirrors Unix behavior where some commands (like `awk`) process line-by-line while others (like `sort`) collect everything first.

## Pipe Variants

### `|` — Universal Pipe

The pipe operator adapts based on whether `~` is used:

**With `~` → Map Pipe (iterate over items):**
```lambda
[1, 2, 3] | ~ * 2      // [2, 4, 6]
{a: 1, b: 2} | ~       // [1, 2] — iterates values
{a: 1, b: 2} | ~#      // ['a', 'b'] — iterates keys
```

**Without `~` → Aggregated Pipe (pass as first argument):**
```lambda
[1, 2, 3] | sum        // sum([1,2,3]) → 6
[3, 1, 2] | sort       // sort([3,1,2]) → [1, 2, 3]
data | take(5)         // take(data, 5)
data | slice(0, 10)    // slice(data, 0, 10)
```

### `where` — Filter Clause

The `where` keyword filters items, keeping only those where the condition is truthy:

```lambda
[1, 2, 3, 4, 5] where ~ > 3
// Result: [4, 5]

users where ~.age >= 18
// Keep only adult users

// Chained with pipes
data | ~.name where len(~) > 3 | upper(~)
```

### Why `where` Instead of `|?`

| Option | Example | Pros | Cons |
|--------|---------|------|------|
| **`where`** ✓ | `data where ~ > 5` | SQL-familiar, readable, extensible | Keyword vs operator |
| `\|?` | `data \|? ~ > 5` | Short, operator-based | Cryptic, `?` overloaded |
| `\| filter(...)` | `data \| filter(~ > 5)` | Explicit | Verbose |
| `[predicate]` | `data[~ > 5]` | XPath-like | Conflicts with indexing |

**Precedents:**

| Language | Filter Syntax | Notes |
|----------|---------------|-------|
| SQL | `WHERE` | Universal familiarity |
| LINQ (C#) | `.Where()` | Method, but same keyword |
| PowerShell | `Where-Object` | Cmdlet with `Where` name |
| XPath | `[predicate]` | Bracket syntax |
| XQuery | `where` clause | Part of FLWOR |

### Future Query Clauses (Roadmap)

Using `where` as a keyword opens the door to additional **query clauses** inspired by SQL and XQuery's FLWOR expressions:

```lambda
// Current: pipe + where
data | ~.amount where ~ > 100 | sum

// Future potential clauses:
data
    | ~.amount
    where ~ > 100           // filter
    order ~ desc            // sort (future)
    limit 10                // take first N (future)
    offset 5                // skip first N (future)
    | sum

// XQuery FLWOR-style expressions (future consideration)
for (user in users)
    where user.active
    order user.name
    limit 100
    return {name: user.name, email: user.email}
```

**XQuery FLWOR reference:**
- **F**or — iteration
- **L**et — variable binding  
- **W**here — filtering
- **O**rder by — sorting
- **R**eturn — projection

Lambda's `for` expression + `where` clause provides the foundation. Future additions (`order`, `limit`, `offset`, `group`) would create a powerful, SQL/XQuery-like query syntax while maintaining Lambda's functional nature.

## Grammar Changes

### New Productions

```
pipe_expression
    : expression
    | pipe_expression '|' expression      // pipe operator
    | pipe_expression 'where' expression   // filter clause
    ;

primary_expression
    : ... existing rules ...
    | '~'   // current item reference
    | '~#'  // current key/index reference
    ;
```

### Precedence

Pipe and `where` have **low precedence**, just above assignment:

```
// Precedence (high to low)
1.  () [] .           - Primary
2.  - + not           - Unary
3.  ^                 - Exponentiation
4.  * / div %         - Multiplicative
5.  + -               - Additive
6.  < <= > >=         - Relational
7.  == !=             - Equality
8.  and               - Logical AND
9.  or                - Logical OR
10. to                - Range
11. is in             - Type predicates (restricted: T, !T, T? only)
12. |                 - Pipe operator (NEW)
13. where             - Filter clause (NEW)
14. = :=              - Assignment
```

Note: Type operators `|`, `&`, `*`, `+` are only valid in **type contexts** (type declarations, annotations, function signatures), not in normal expressions.

## Implementation Considerations

### AST Nodes

```cpp
enum PipeKind { PIPE_MAP, PIPE_COLLECT, PIPE_FILTER };

struct PipeExpr : Expr {
    PipeKind kind;
    Expr* left;      // input expression
    Expr* right;     // expression containing ~ references
};

struct CurrentRef : Expr {
    // represents ~ reference to current item
    // resolved at runtime based on pipe context
};
```

### Transpilation Strategy

**Map pipe** desugars to for-expression:

```lambda
// Source
[1, 2, 3] | ~ * 2

// Desugared
(let __ctx = [1, 2, 3],
 if (__ctx is collection)
   (for (__item in __ctx) (let ~ = __item, ~ * 2))
 else
   (let ~ = __ctx, ~ * 2))
```

**Aggregated pipe** desugars to function call:

```lambda
// Source
data | ~ * 2 | sum

// Desugared  
(let __pipe1 = (for (~ in data) ~ * 2),
 sum(__pipe1))
```

### Scoping Rules

- `~` is only valid within the right-hand side of a pipe expression
- Nested pipes create nested scopes:
  ```lambda
  outer | inner | ~  // ~ refers to inner's current item
  ```
- To reference outer `~`, use let binding:
  ```lambda
  outer | (let outer_item = ~, inner | ~ + outer_item)
  ```

## Comparison with XPath

| XPath | Lambda Pipe | Description |
|-------|-------------|-------------|
| `/` | `\|` | Navigate/transform |
| `.` | `~` | Current item |
| `//` | recursive pattern | Descendant axis |
| `[@attr]` | `where ~.attr` | Predicate filter |
| `[position()]` | `~#` | Position access |

```xpath
//book[price>30]/author/name
```

```lambda
doc | <book> where ~.price > 30 | ~.author | ~.name
```

## Comparison with Alternatives

### vs. Explicit map/filter

```lambda
// Explicit (current Lambda)
map((x) => x * 2, filter((x) => x > 0, data))

// Set-oriented pipe (proposed)
data where ~ > 0 | ~ * 2
```

The pipe version reads left-to-right and is more concise.

### vs. For Expressions

```lambda
// For expression
for (x in data) if (x > 0) x * 2 else null

// Pipe
data where ~ > 0 | ~ * 2
```

Pipes are better for chained transformations; for-expressions for complex logic.

### vs. Vector Arithmetic

```lambda
// Vector arithmetic (simple cases)
data * 2

// Pipe (complex expressions)
data | ~.value * 2 + ~.bonus
```

Pipes extend vector semantics to arbitrary expressions.

## Migration & Compatibility

- **Backward Compatible:** `|` as pipe and `~` are new syntax
- **Type pattern change:** `|`, `&`, `*`, `+` restricted to type contexts only
- **`is` expression:** Only `T`, `!T`, `T?` allowed (not `T | U`, `T & U`, etc.)
- **Opt-in:** Existing code using type aliases continues to work

## Summary

| Feature | Syntax | Description |
|---------|--------|-------------|
| Pipe (with `~`) | `\|` | Auto-iterate over collection, `~` = current item |
| Pipe (no `~`) | `\|` | Pass whole collection to aggregator/function |
| Filter Clause | `where` | Keep items where expression is truthy |
| Current Item | `~` | Reference to item being processed |
| Current Key/Index | `~#` | Key (maps) or index (arrays/lists) |

**Type Pattern Contexts:**

| Context | Full Patterns (`\|`, `&`, `*`, `+`) | Restricted (`T`, `!T`, `T?`) |
|---------|-------------------------------------|------------------------------|
| `type T = ...` | ✓ | ✓ |
| `let x: T` | ✓ | ✓ |
| `fn (x: T) T` | ✓ | ✓ |
| `for (x: T in ...)` | ✓ | ✓ |
| `x is T` | ✗ | ✓ |

This design:
- **Rejects** scalar pipes as inconsistent with Lambda's philosophy
- **Embraces** set-oriented semantics matching Lambda's vector arithmetic
- **Follows Unix tradition**: lines of text → items of collection
- **Draws from** Unix shell (stream processing), XPath (markup navigation), PowerShell (object pipeline), jq (JSON streams)
- **Provides** concise, readable syntax for data transformation pipelines

## References

- [Unix Pipeline](https://en.wikipedia.org/wiki/Pipeline_(Unix))
- [XPath Axes and Node Sets](https://www.w3.org/TR/xpath-31/)
- [XQuery FLWOR Expressions](https://www.w3.org/TR/xquery-31/#id-flwor-expressions)
- [PowerShell Pipeline](https://docs.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_pipelines)
- [jq Manual](https://stedolan.github.io/jq/manual/)
- [LINQ (C#)](https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/concepts/linq/)
- [Raku Hyper Operators](https://docs.raku.org/language/operators#Hyper_operators)
