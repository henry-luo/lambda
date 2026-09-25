# Lambda Set-Oriented Pipe Operator Proposal

> **Status (2026-09-25):** the pipe shipped as `|>` (S10.1.1, S10.1.2v2) and the
> filter has been respelled twice — `where` → `that` (S10.1.5v2) → **`|:`**
> (implemented 2026-09-25), with `that` kept as the single-value proviso. The ruling and its reasoning are
> in *Filter Stage `|:` and the `that` Proviso* below; the original `|`/`where`
> text in the body is kept as history and the superseded filter section is in
> Appendix S. Read `|` in the older examples as `|>`.

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
| `grep pattern`       | `\|: condition`        | Filter                 |
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

## Filter Stage `|:` and the `that` Proviso (ruled 2026-09-23)

> **Status:** ruled 2026-09-23 and ratified into the formal spec at
> **v34.0.0** — S10.1.5v3 (`that` proviso), S10.1.6 (`|:` filter stage),
> S10.1.2v4 (single-mode; array out), S10.3.1v3 (`where` diagnostic names
> `|:`), and **S2.5.7v2** (a list is built, never computed — §F.6), with
> S7.10.1v3/S7.10.5v3 following; cross-refs S2.5.5v2, S2.5.8, S8.2.4,
> S8.3.1v3, S8.4.1v2, S10.1.3, S10.2.1, S10.6.1, SO38.
> **Implemented 2026-09-25 (spec v37.0.3)** on both tiers: the `|:` token and
> `grammar.js` row, `OPERATOR_FILTER`/`OPERATOR_THAT` in place of
> `OPERATOR_WHERE`, E238 for a `|:` body with no free `~`, the `where`
> diagnostic, and the migration of 133 `that`-filter sites in 27 files (the
> earlier "152 fixtures" count was not reproducible; a parser census found 133).
> The §F.6 array result landed the same day with the whole S2.5.7v2 row: both
> pipes and every sequence function return an array for a list source, while
> the broadcast operators keep the operand kind. The implicit-field question
> the rulings left open (SO47) was ruled the same day as **S10.1.7** (spec
> v37.1.0, §F.7): a single-subject body may leave `~` implicit, the pipe
> family always spells it, and an implicit read never supplies a callee —
> which S10.1.5v3's own `xs that len(~) > 2` needs. The set operators
> `| & !` keep P2's operand-kind rule until SO48 is ruled.

### §F.1 What was wrong with `that`

The expression-level `that` (`c that p`, S10.1.5v2) was a filter: it walked
the members of `c` and kept the truthy ones. Three problems, in the order
they were raised:

1. **One keyword, two arities.** `T that cond` in type position and
   `for … where cond` are both *one value, one predicate* — they refine a
   single binding. `c that p` was *many members, one predicate* — a
   sequence walk. Every "detail" in which the two differed (`~`/`~#`
   binding per member, kind preservation, list collapse, text walking by
   code point) was a consequence of the second being a walk. Readers saw one
   word and two rule sets.
2. **A pipe stage that did not look like one.** The filter was already a
   pipe-family operator in every respect but spelling: pipe precedence tier,
   left-associative, `~`/`~#` under S10.1.3's innermost-wins, walking what
   `|>` walks. A bare word between two `|>` stages reads as a clause, not a
   stage, so users did not relate the two.
3. **A return-type table of its own.** S10.1.5v2 carried a four-row kind
   table (list→list-or-collapse, array/range/map/element→`[]`, text→`""`,
   scalar→`T?`) and S8.2.4 had to state that the type-subscript rule was
   *deliberately not* the filter rule. Two rules that happened to agree with
   the mapping pipe's, maintained separately.

### §F.2 Prior art: pipe and filter data models

Extends the "Design Inspiration" survey above with the filter half. The
question each row answers: *what does a filter give back, and how does the
language keep the container kind straight?*

| Language | Pipeline data model | Filter returns | Empty / single item |
|---|---|---|---|
| **Haskell / F# / Elm / Rust / LINQ / Kotlin** | typed sequence `[a]`, `Seq<T>`, `IEnumerable<T>` | `filter :: (a→Bool) → [a] → [a]` — same container, same element type, usually lazy | `[]` or a one-element sequence; never collapses |
| **XPath / XQuery** | one flat **sequence** type; an item *is* a singleton sequence; sequences never nest | `e/node()[p]` → sequence of the same items | `()`; a singleton ≡ the item, so "collapse" is an identity of the model, not a rule |
| **PowerShell** | objects flowing down a pipeline; collections are *unrolled* on emit | `Where-Object` emits the survivors | collected as `$null` / the object / `Object[]` — the well-known `@(…)` footgun |
| **jq** | a **stream** of values, not a collection | `select(p)` emits its input or nothing; `.[] \| select(p)` is a stream; `map(select(p))` rebuilds an array | empty stream / one value |
| **Nushell** | structured list, record, table | `where` keeps the input's kind — table→table, list→list | empty table / list |
| **R dplyr** | data frame | `filter` → data frame, columns and grouping kept | 0-row frame |
| **SQL** | relation | `WHERE` → relation with the same heading | empty relation |
| **Clojure** | seq abstraction over any collection | `filter` on a **map** yields a seq of `[k v]` entries — map-ness lost | `()` |
| **Kotlin / Haskell `Data.Map` / Rust `retain`** | keyed container | `filter` on a map **returns a map**, keys kept | empty map |

Three facts fall out:

- **Element type preservation is universal in typed languages.** No
  language makes `filter` return an untyped bag.
- **Container-kind preservation is the norm.** Only XPath and PowerShell
  collapse, for opposite reasons — XPath because item ≡ singleton sequence
  is a theorem of its model; PowerShell because unrolling is a side effect
  of emit. Lambda's list under S2.5.5v2 (one-item list *is* the item, empty
  is `null`) is the XPath kind of collapse: principled, not accidental.
- **Maps split the field.** Kotlin/Haskell/Rust keep keys; Clojure and JS
  (`Object.entries(m).filter`) drop them. Lambda's SO38 (keys dropped, array
  out) is the Clojure choice — defensible, and the one row where a user from
  a typed language will be surprised.

The design consequence: **a filter is `Seq<T> → Seq<T>` and has no data
model of its own.** It is the mapping pipe minus the items that fail. That
is what §F.4 rules.

### §F.3 Syntax options and the ruling: `|:`

Every spelling considered, including the two that were shipped and the one
before them. Constraints: must read as a pipe-family stage in a chain
(`xs ⟨filter⟩ p |> f`); must not spend a glyph another family owns; must
not echo an unrelated operator.

| Spelling | Example | For | Against | Verdict |
|---|---|---|---|---|
| `where` | `xs where ~ > 0` | SQL-familiar | collided with the `for`-header `where` clause | retired (S10.3.1v2) |
| `that` | `xs that ~ > 0` | reads as English | one keyword, two arities (§F.1); not visibly a pipe stage | **retained for the single-value proviso only** (§F.5) |
| `\| filter(…)` | `xs \|> filter(~ > 0)` | explicit | verbose; makes filter a library call the pipe has to special-case | rejected |
| `[pred]` | `xs[~ > 0]` | XPath predicate | ambiguous with indexing and with the `e[T]` type subscript (S8.2.4) | rejected |
| `\|>[ expr ]` | `xs \|>[~ > 0]` | pipe-shaped | same `[]` ambiguity | rejected |
| `\|?` | `xs \|? ~ > 0` | short | `?` belongs to the optional family (`T?`, `c? in`, `e?T`); reads as null-safe pipe | rejected |
| `?>` | `xs ?> ~ > 0` | short, `?` connotes test | drops the `\|`, so it does not look like a stage; `?` family reserved | rejected |
| `\|?>` / `\|>?` | `xs \|?> ~ > 0` | visibly `\|>`-shaped, `?` = test | three characters, heavy; `\|>?` reads as optional chaining | rejected |
| `:>` | `xs :> ~ > 0` | short, free | mirrors `<:` (`is_in`, `grammar.js` operator table) — the two would be read as a pair they are not; F#/OCaml upcast glyph; says nothing about pipes | rejected |
| `\|\|>` | `xs \|\|> ~ > 0` | pipe-shaped; not currently lexed (only `\|` and `\|>` are) | `\|` is union everywhere (S10.1.1) so it reads "union pipe"; F# tuple pipe | rejected |
| `\|:>` | `xs \|:> ~ > 0` | pipe-shaped, chains with `\|>` | it is `\|` + `:>`, so the `<:` echo survives; three characters | rejected |
| **`\|:`** | `xs \|: ~ > 0` | see below | no `>` | **ruled** |

Why `|:` wins, and why the missing `>` is a feature:

- **It has the right precedent.** Set-builder notation writes "such that"
  as `|` or `:` — `{x ∈ S | P(x)}`, `{x ∈ S : P(x)}`. `S |: P` spells
  "S such that P", the same phrase `that` carries in type position. The two
  proviso forms rhyme: `T that cond` for one value, `xs |: cond` for a
  sequence.
- **Dropping `>` is honest.** In `|>` the arrow says "flows into the body":
  an item goes in, a different value comes out. A filter does not transform;
  it selects. A `>` would claim a data flow that is not there.
- **Chains still read as stages.** `users |: ~.active |> ~.name` — "such
  that", "then". The shared leading `|` is what makes it pipe-family.
- **It is free.** Only `|` and `|>` are lexed today; `|:` collides with
  nothing in expression, type, or path syntax.

Lexing note, decided rather than discovered: longest match wins, and the only
adjacent-`|:` collision is a `match` arm written `case a |: …`, which nobody
writes (`case int | string: expr` has whitespace and is unaffected). A
negative fixture covers the glued form.

### §F.4 Semantics of `|:`

- **It is a pipe stage.** `c |: p` walks exactly what `c |> body` walks
  (S10.1.2v2: array or list by item, map by value, element by attribute
  values then children, text by code point, a non-sequence scalar as one
  member) and keeps exactly the members for which `p` is truthy. `~` is the
  member, `~#` its key or index, scoped by S10.1.3.
- **Result kind is the pipe's, by reference.** There is no filter kind table.
  Whatever S10.1.2v4 and S2.5.7v2 say the mapping pipe returns for a source,
  `|:` returns the same holding the survivors — and since §F.6 that is an
  **array for every sequence source, a list included** (`[]` when none
  survive); text gives its own kind (`""` when empty, S2.5.8); a scalar
  gives itself or `null`. A map's keys are dropped (SO38).
- **Single-mode: a free `~` is required.** `|>` is dual-mode (S10.1.2v2): a
  `~`-free body is whole-value application (`data |> sum`). Whole-value
  filtering is meaningless, and letting `|:` read the same `~`-free text as
  per-item application (`xs |: is_even` ≡ `xs |: is_even(~)`) would make the
  two pipes disagree on the same syntax. So a `|:` body with no free `~` is
  a compile error (new E-code: "filter body must mention `~`"). The
  per-item-callable reading was considered and rejected on that ground; the
  idiom is `xs |: is_even(~)`.
- **Precedence and associativity are unchanged** from `that`: pipe tier,
  left-associative, so `c |: p |> f |: q` chains left to right.
- **Static type** is `T[]` for a source of element type `T` (`int[]` for a
  range, `any[]` for map/element), the text kind for text, `T?` for a scalar
  — with the element type held fixed, since a filter never changes what an
  item is. Sound for a list source too: the result is an array (§F.6), so it
  cannot collapse. Only an `any` source stays open.

### §F.5 `that` is the single-value proviso — and how it differs from the type constraint

`that` survives with one meaning, aligned with its type-position meaning:
**`x that cond` is `x` when `cond` (with `~` bound to `x`) is truthy, and
`null` otherwise.** The left operand is one item whatever it is — a
collection on the left is *not* walked: `xs that len(~) > 2` is the whole
array or `null`. This is the guard/proviso operator the language lacked, and
it is the old S10.1.5v2 scalar branch (`5 that ~ > 9` is `null`) promoted to
the whole rule, with the sequence branch moved to `|:`.

Why `null` and not `error` on a failed proviso:

- **A failed test is absence, not failure.** S7's discipline is that every
  error value is deliberate: something went wrong that must be discharged.
  "x, provided that …" — when the proviso fails there is no x, and nothing
  broke. That is what `null` is.
- **`error` would make every proviso a must-handle site.** A position that
  textually admits `error` must engage it (S7), so `x that p` would need
  `^ {…}` or a `T^` contract everywhere, and `if (x that p)` — the common
  shape — would be an unhandled-error compile error. `null` composes with the
  whole `?` family for free: `(x that p) ?? default`, `x that p |> f`,
  truthiness in `if`.
- **The type checker already produces `T?`** for the scalar case
  (`lambda_type_nullable_normalized` in `build_ast.cpp`), and `T?` is
  `T | null`, not `T | error`.

The subtle difference to keep straight — same keyword, same predicate, same
`~` binding, **different failure channel**:

| Form | Position | On mismatch | Type |
|---|---|---|---|
| `x that cond` | expression | **`null`** — absence; nothing to handle | `T?` |
| `let x: T that cond`, `f(x: T that cond)`, `{name: string that len(~) > 0}` | type binding / contract | **soft error** — the contract fails as any type contract does (S11), reported at the binding or call boundary | `T` on success; the binding or call carries the error |

The two are not in conflict: the expression form *asks* whether `x`
satisfies the proviso and answers with `x` or nothing; the type form
*asserts* that it does, and an assertion that fails is an error. Same
predicate, one is a query and the other a contract. Readers coming from the
old filter meaning should note that `xs that p` no longer walks `xs` — that
is `xs |: p` now — and that the parser rejects nothing here: `xs that p` is
legal and means "all of `xs`, or `null`", which is a silent semantic change
for any old filter site. The fixture sweep converts every old filter site to
`|:` for that reason.

### §F.6 Result kind: three options, ruled "unify as array"

The last question was what a pipe or filter returns when its source is a
**list** — `(1, 2, 3) |> ~ * 2`, `(1, 2, 3) |: ~ > 1` — given that a list is
a special array (the spread bit, S2.5.1v2): `(1, 2, 3)` *is* `[1, 2, 3]` in
every array position, but `[1, 2, 3]` is not `(1, 2, 3)`. Three probes on the
2026-09-23 build framed it:

```lambda
let a = [1, 2, 3];  let l = (1, 2, 3);
len(<d a>) → 1        len(<d l>) → 3        len(<d *a>) → 3
len([l |> ~ * 2, 9]) → 4                    // mapped list still splices
len(<d (a that ~ > 1)>) → 1                 // array source in content: one child
fn f(a: int[]) => len(a);
f((1, 2, 3) that ~ > 2)         → E201 "expected int[], got int 3"
let y: int[] = take((1, 2, 3), 1) → E201 "expected int[], got int 1"
```

Two facts drove the ruling. **A list already passes a `T[]` contract — until
it shrinks**: a ≥2 list is admitted as the array it is, but the moment any
shrinking operation (`|:`, `take`, `slice`, `unique`, `drop`) leaves 0 or 1
item, S2.5.5v2 collapses it to `null` or the item and the next `T[]` boundary
throws E201. The hole is *collapse × shrinker*, not `|:` and not the
annotation; `|>` was sound all along (a list is ≥2 by construction and a
mapping never shrinks). **And the primary pipe use case already needed
`*`**: an array source in content gives one child, so anyone piping data into
an element writes `*(…)` or a `for` today.

| Option | Rule | Static type of `T[] ⟨op⟩ …` | Content | Verdict |
|---|---|---|---|---|
| **1. Follow the input** (S2.5.7 P2, 2026-09-23) | list in → list out, array in → array out, mixed → array | open for every shrinker (`|:`, `take`, `slice`, `unique`, `drop`) — the result may be a collapsed item or `null`; a latent E201 at the next `T[]` boundary | `<d (l |: p)>` splices | consistent, but the shrinker trap cannot be fixed inside it: the collapse *is* the list rule |
| **2. Unify as array** | every function and pipe returns an array for any sequence input; lists are built by syntax only | `T[]`, always; sound at every boundary; chains through multi-step pipes | `<d *(l |: p)>` or `for … where` — the idiom the array case already uses | **ruled** |
| **3. Unify as list** | every function and pipe returns a list | open everywhere; `len(xs |: p)` with one survivor is `len(3)` = 0 (S8.3.1v3); `[xs |> f, 9]` splices unexpectedly | splices | rejected — data processing becomes a minefield to buy nothing |

**The ruling (S2.5.7v2): a list is built, never computed.** The list kind
belongs to construction — a list literal, a `for` expression, a block,
content, a query (S8.2.4) — because those *compose values and content* and
must splice where they land. Everything that *processes* a sequence returns
an array: the mapping pipe and `|:`, `sort`, `reverse`, `unique`, `take`,
`drop`, `slice`, `[i to j]`, `zip`, `fill`, `split`, `find`, `varg()`, rest
parameters, `content(e)`, `keys`/`values`/`names`, the vectorized sys funcs.
`T[]` as the return type is simpler, consistent, easier to join in
multi-step pipes, and matches every typed language in §F.2. The division of
labour is clean: **`for … where` is the content filter, `|:` is the data
filter.** This reverses the list half of P2 of the list fixes (the same
day); `take((10, 20, 30), 1)` becomes `[10]`.

**Operators are the one carve-out, and it is principled.** `+ - * /`, the
mask comparisons `eq ne lt le gt ge`, and `++` are *broadcast notation*, not
processing — `(1, 2, 3) + 1` reads as "the same list, adjusted", not as a
function call — so they keep the operand kind: a scalar operand leaves the
kind alone, list `⊕` list is a list, and any array or range operand gives an
array (the list is demoted).

```lambda
(1, 2, 3) + 1          → (2, 3, 4)      // kind kept
[1, 2, 3] + 1          → [2, 3, 4]
(1, 2, 3) + [1, 2, 3]  → [2, 4, 6]      // list demoted
(1, 2) ++ 3            → (1, 2, 3)
(1, 2) ++ [3]          → [1, 2, 3]
```

This is *sound* for exactly the reason option 1 was not: **no operator
shrinks**. Element-wise arithmetic and the masks return as many lanes as the
list operand had, and `++` only grows, so an operator result is never a
collapsing one-item list and a `T[]` static type stays honest through it.
The line is therefore not "operators vs functions" by fiat — it is "can it
shrink?", and operators happen to be precisely the sequence operations that
cannot. Text is untouched throughout (S2.5.8): a text kind is a *type*, not
a bit.

### §F.7 Implicit fields: single-subject bodies only (ruled 2026-09-25)

> **Ruling:** option (A) below, USER 2026-09-25, formalized as **S10.1.7**
> (spec v37.1.0); closes SO47.

**The question.** The old `that` filter carried a convenience over from
object constraints: in its body an unbound bare name read as a field of `~`
(`users that age >= 18`). Once the filter moved to `|:` (§F.3) the
convenience had to go somewhere — to `|:`, to the proviso `that`, to both,
or to neither. The method body already answers "must a field be reached
through `~`?" one way: inside `type P { x: int, fn get() => x }` a field is
spoken bare, the OOP receiver convention.

| Option | `\|:` body | `that` body | Method body | Verdict |
|---|---|---|---|---|
| **(A) single-subject bodies only** | `~` required | implicit fields | implicit fields | **ruled** |
| (B) `~` wherever a `~` exists | `~` required | `~` required | implicit fields | rejected — splits the proviso from the object constraint and methods it sits beside |
| (C) `~` optional everywhere | implicit fields | implicit fields | implicit fields | rejected — `\|>` cannot join, so it is not uniform, and it reopens E238 |

**Why the pipe family always spells `~`.**

1. **`|>` cannot have implicit fields at all.** S10.1.2v4 tells mapping from
   whole-value application by whether the body's *text* spells a free `~`.
   If an unbound name could supply a `~` behind the text, the mode would
   depend on binding state: `xs |> age` is `age(xs)` while a function `age`
   is in scope, and silently becomes the mapping `xs |> ~.age` when it is
   renamed or deleted. So "same family, same rule" has one consistent
   reading: both pipes spell `~`.
2. **Single-mode was the point of `|:` (S10.1.6).** `xs |: active` reading
   `~.active` beside `xs |> active` reading `active(xs)` is exactly the one
   text read two ways that S10.1.6 exists to prevent.
3. **E238 depends on it.** The free-`~` check runs on the resolved body.
   With implicit fields on, a misspelled `xs |: is_evne` would resolve to
   `~.is_evne`, pass the check, read `null` for every member and filter to a
   silent `[]`. With them off, it is E238 at compile time.
4. **A pipe body is about every member, and pipes nest.** In
   `orders |: len(~.lines |: qty > 0) > 0`, which `~` would a bare `qty`
   read? S10.1.3's innermost-wins answers, but the reader has to work it
   out; `~.qty` says it.

**Why the proviso keeps them.** A method, an object-level constraint and the
proviso are each *about one subject*, as a method is about its receiver.
S10.1.5v3 makes the proviso "the same predicate, binding, and word" as the
type constraint, and the object-level constraint
`type User { name: string, that name != "admin" }` sits beside the methods
in a type body and reads `name` as they do. Requiring `~` in the expression
`that` alone would give one keyword two name rules by position — the §F.1
defect again, in another dimension.

**Why not (C).** It buys only the `~.` of a filter body. It cannot be
uniform, because `|>` is excluded structurally (point 1), so it would make
`|:` the one member of its family with implicit fields, and it reopens the
E238 hole (point 3).

**Two resolution orders, one principle.** Where the subject's fields are
*declared* — a method, an object constraint — they are names of the body's
scope and shadow outer bindings, as members shadow globals in Java or C++:
with a module-level `let x = 100`, `P.get()` above returns the field. The
proviso's subject has no declared shape — `x that p` takes any value — so a
bare name there is the member `~.name` only when no binding claims it:
`{x: 5} that x == 5` compares the module's `x`, 100. Letting a dynamic field
shadow outer bindings would make every outer name capturable by whatever
keys a value happens to carry.

**Callee names.** An implicit read never supplies a callee. Before the
ruling, the rewrite turned the callee of `len(~)` into `~.len`, so a `that`
body could not call a function its scope did not bind: the old filter
`xs that len(~) > 2` kept nothing, and the proviso could never hold. The
resolver now leaves a bare callee to ordinary lookup. (A declared field
holding a function is an ordinary scope name in a method body, so calling it
calls that field; this rule is about the implicit read only.)

**Nesting.** A `|:` body switches implicit fields off even inside a `that`
body — `x that (~.items |: flag(~))` reads `flag` as a name; a `that` inside
a `|:` body reads its own subject — `[{a: 3}] |: (~ that a > 2) != null`
reads `~.a` of each member.

**Open ends.**

- A `|>` body nested in a single-subject body still receives implicit fields:
  the resolver's `that` scope reaches it, so
  `user that len(items |> price) == 2` reads `price` as `~.price` of each
  item where the syntactic test of S10.1.2v4 makes it the application
  `price(items)`. Recorded as S10.1.7's conformance gap; the fix is the one
  `|:` already has — switch the scope off for a `|>` body.
- Type-position `T that cond` outside an object type — an annotation, a
  match arm — reads bare names as ordinary names: `case {a: int} that a > 1`
  never reads `~.a`. Whether it joins the proviso is SO49.
- Implicit reads are unchecked: a misspelled field in a `that` body reads
  `null`. That is a checker question — reject an implicit name the
  subject's static shape cannot have — not a question of which bodies allow
  implicit fields.
- The data-processing design (PD13) held bare column names in verb arguments
  as a possible later extension "via the implicit-field rule". Verb
  arguments are a pipe body, so under S10.1.7 that extension would need the
  ruling revised.

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
| Filter Stage | `\|:` | Keep items where expression is truthy (was `where`, then `that`) |
| Proviso | `that` | `x that cond` is `x` or `null`; one item, not a walk |
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


## Appendix S — Superseded rulings

Struck-through text is kept as history per `doc/Doc_Convention.md` §4; what
replaced it is noted at each heading.

~~### `where` — Filter Clause~~ *(superseded: `where` retired by S10.3.1v2 for
colliding with the `for`-header clause; respelled `that`, then `|:` — see §F.3)*

~~The `where` keyword filters items, keeping only those where the condition is truthy:~~

```lambda
[1, 2, 3, 4, 5] where ~ > 3
// Result: [4, 5]

users where ~.age >= 18
// Keep only adult users

// Chained with pipes
data | ~.name where len(~) > 3 | upper(~)
```

~~### Why `where` Instead of `|?`~~ *(superseded by §F.3)*

| Option | Example | Pros | Cons |
|--------|---------|------|------|
| **`where`** ✓ | `data where ~ > 5` | SQL-familiar, readable, extensible | Keyword vs operator |
| `\|?` | `data \|? ~ > 5` | Short, operator-based | Cryptic, `?` overloaded |
| `\| filter(...)` | `data \| filter(~ > 5)` | Explicit | Verbose |
| `[predicate]` | `data[~ > 5]` | XPath-like | Conflicts with indexing |

~~**Precedents:**~~

| Language | Filter Syntax | Notes |
|----------|---------------|-------|
| SQL | `WHERE` | Universal familiarity |
| LINQ (C#) | `.Where()` | Method, but same keyword |
| PowerShell | `Where-Object` | Cmdlet with `Where` name |
| XPath | `[predicate]` | Bracket syntax |
| XQuery | `where` clause | Part of FLWOR |

~~### Future Query Clauses (Roadmap)~~ *(superseded: `order by`, `limit`, `offset`, `group by` landed as `for`-header clauses — S14, `Lambda_Expr_For_Clauses2.md`)*

~~Using `where` as a keyword opens the door to additional **query clauses** inspired by SQL and XQuery's FLWOR expressions:~~

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

~~**XQuery FLWOR reference:**~~
~~- **F**or — iteration~~
~~- **L**et — variable binding  ~~
~~- **W**here — filtering~~
~~- **O**rder by — sorting~~
~~- **R**eturn — projection~~

~~Lambda's `for` expression + `where` clause provides the foundation. Future additions (`order`, `limit`, `offset`, `group`) would create a powerful, SQL/XQuery-like query syntax while maintaining Lambda's functional nature.~~

