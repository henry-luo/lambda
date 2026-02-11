# Lambda Script: Extended For Expressions

This document proposes extensions to Lambda's `for` expression and statement syntax, adding query-style clauses inspired by XQuery's FLWOR expressions, SQL, and other functional languages.

---

## Table of Contents

1. [Motivation](#motivation)
2. [Design Inspiration](#design-inspiration)
   - [XQuery FLWOR Expressions](#xquery-flwor-expressions)
   - [SQL SELECT Statements](#sql-select-statements)
   - [C# LINQ Query Syntax](#c-linq-query-syntax)
   - [Scala for-comprehensions](#scala-for-comprehensions)
   - [Haskell List Comprehensions](#haskell-list-comprehensions)
3. [Proposed Syntax](#proposed-syntax)
   - [For Expression with Clauses](#for-expression-with-clauses)
   - [For Statement with Clauses](#for-statement-with-clauses)
4. [Clause Specifications](#clause-specifications)
   - [where Clause](#where-clause)
   - [order by Clause](#order-by-clause)
   - [group by Clause](#group-by-clause)
   - [limit and offset Clauses](#limit-and-offset-clauses)
   - [let Clause](#let-clause-within-for)
5. [Clause Ordering and Semantics](#clause-ordering-and-semantics)
6. [Compatibility with Pipe Operator](#compatibility-with-pipe-operator)
7. [Examples](#examples)
8. [Grammar Changes](#grammar-changes)
9. [Implementation Considerations](#implementation-considerations)
10. [Summary](#summary)

---

## Motivation

Lambda's current `for` expression provides basic iteration:

```lambda
for (x in data) x * 2
```

However, real-world data processing often requires:
- **Filtering** — process only items matching criteria
- **Sorting** — control output order
- **Grouping** — aggregate items by key
- **Limiting** — take first N results
- **Intermediate bindings** — compute values for reuse

Currently, these require nested expressions or external function calls:

```lambda
// Current: verbose and hard to read
sort(
  for (user in users) 
    if (user.age >= 18) 
      {name: user.name, age: user.age} 
    else null,
  (a, b) => a.age - b.age
) | take(10)
```

With extended `for` clauses:

```lambda
// Proposed: declarative and readable
for (user in users
     where user.age >= 18
     order by user.age
     limit 10)
  {name: user.name, age: user.age}
```

---

## Design Inspiration

### XQuery FLWOR Expressions

XQuery's **FLWOR** (For-Let-Where-Order by-Return) is the primary inspiration:

```xquery
for $book in //book
let $price := $book/price
where $price > 30
order by $book/title ascending
return <result>{ $book/title }</result>
```

**Key characteristics:**
- Declarative clause-based syntax
- Clear separation of iteration, binding, filtering, ordering, and projection
- Clauses appear in a defined order
- Each clause operates on the tuple stream from previous clauses

### SQL SELECT Statements

SQL provides familiar query semantics:

```sql
SELECT name, age
FROM users
WHERE age >= 18
GROUP BY department
HAVING COUNT(*) > 5
ORDER BY age DESC
LIMIT 10 OFFSET 20
```

**Key characteristics:**
- `WHERE` filters rows before grouping
- `GROUP BY` aggregates rows
- `HAVING` filters after grouping
- `ORDER BY` sorts final results
- `LIMIT`/`OFFSET` for pagination

### C# LINQ Query Syntax

LINQ provides a C#-native query syntax:

```csharp
var results = from user in users
              where user.Age >= 18
              orderby user.Age descending
              select new { user.Name, user.Age };
```

**Key characteristics:**
- Integrates into host language naturally
- Type-safe with IntelliSense support
- `let` for intermediate bindings
- `into` for continuation queries

### Scala for-comprehensions

Scala's for-comprehensions combine iteration with guards and bindings:

```scala
for {
  user <- users
  if user.age >= 18
  name = user.name.toUpperCase
} yield (name, user.age)
```

**Key characteristics:**
- Guards (`if`) for filtering inline
- `=` for intermediate bindings
- `yield` for producing results
- Desugars to `flatMap`/`map`/`filter`

### Haskell List Comprehensions

Haskell's list comprehensions provide concise syntax:

```haskell
[ (name, age) | user <- users, age <- [user.age], age >= 18, let name = user.name ]
```

**Key characteristics:**
- Guards are boolean expressions separated by commas
- `let` for local bindings
- Multiple generators for nested iteration

---

## Proposed Syntax

### For Expression with Clauses

```
for (<bindings> [where <expr>] [order by <ordering>] [group by <grouping>] [limit <n>] [offset <n>]) <body-expr>
```

Where:
- `<bindings>` — one or more iteration bindings: `name in expr, name2 in expr2, ...`
- `where <expr>` — filter condition (boolean expression)
- `order by <ordering>` — sort specification with optional direction
- `group by <grouping>` — grouping specification
- `limit <n>` — maximum number of results
- `offset <n>` — skip first N results
- `<body-expr>` — expression evaluated for each (filtered, sorted) item

### For Statement with Clauses

```
for <bindings> [where <expr>] [order by <ordering>] [group by <grouping>] [limit <n>] [offset <n>] {
    <statements>
}
```

The same clauses apply to for-statements with block bodies.

---

## Clause Specifications

### where Clause

The `where` clause filters items based on a boolean condition.

#### Syntax

```
where <boolean-expr>
```

#### Semantics

- Evaluated for each iteration tuple
- Only tuples where `<boolean-expr>` is truthy proceed
- Can reference any bound variable from iteration bindings
- Multiple conditions combine with `and`:
  ```lambda
  where cond1 and cond2 and cond3
  ```

#### Examples

```lambda
// Filter by condition
for (x in 1 to 100 where x % 2 == 0) x
// [2, 4, 6, ..., 100]

// Multiple conditions
for (user in users where user.active and user.age >= 18)
  user.name

// With multiple bindings
for (x in xs, y in ys where x + y > 10) {x: x, y: y}
```

#### Comparison: `where` vs `if` in body

| Approach | Behavior | Use Case |
|----------|----------|----------|
| `where` clause | Filters before body evaluation | Clean filtering, no nulls |
| `if` in body | Conditional output per item | May produce nulls/skipped items |

```lambda
// Using where (cleaner)
for (x in data where x > 0) x * 2
// Result: [2, 4, 6] — only positive inputs

// Using if (produces nulls)
for (x in data) if (x > 0) x * 2 else null
// Result: [null, 2, null, 4, null, 6] — needs filtering
```

---

### order by Clause

The `order by` clause sorts the iteration results.

#### Syntax

```
order by <expr> [asc | desc] [, <expr> [asc | desc], ...]
```

#### Semantics

- Sorts the entire result set after filtering
- Default direction is `asc` (ascending)
- Multiple sort keys for tie-breaking
- Null handling: nulls sort first in ascending, last in descending

#### Examples

```lambda
// Simple ascending sort
for (user in users order by user.name) user
// Users sorted by name A-Z

// Descending sort
for (score in scores order by score desc) score
// [100, 95, 87, 72, ...]

// Multiple sort keys
for (employee in employees order by employee.department asc, employee.salary desc)
  {name: employee.name, dept: employee.department, salary: employee.salary}
// Sorted by department, then by salary within each department

// Sort by computed expression
for (item in items order by len(item.name) desc, item.name asc)
  item
// Longest names first, alphabetical within same length
```

#### Direction Keywords

| Keyword | Meaning | Aliases |
|---------|---------|---------|
| `asc` | Ascending (A→Z, 0→9) | `ascending` |
| `desc` | Descending (Z→A, 9→0) | `descending` |

---

### group by Clause

The `group by` clause aggregates items by key.

#### Syntax

```
group by <key-expr> as <group-name>
```

Or with multiple keys:

```
group by <key1>, <key2>, ... as <group-name>
```

#### Semantics

- Groups items sharing the same key value(s)
- The `as <group-name>` clause is **required** and binds a map with `key` and `items` fields
- `<group-name>.key` — the grouping key value (or array of values for multiple keys)
- `<group-name>.items` — array of all items in the group
- Body expression is evaluated once per group

#### Group Structure

The `as <name>` binding produces a map for each group:

```lambda
// <name> is bound to:
{
  key: <grouping-key-value>,      // single value or [key1, key2, ...] for multiple keys
  items: [<items-in-group>]       // array of original items
}
```

#### Examples

```lambda
// Group users by department
for (user in users group by user.department as g)
  {department: g.key, count: len(g.items), members: g.items | ~.name}
// [{department: "Engineering", count: 5, members: [...]}, ...]

// Aggregate within groups
for (sale in sales group by sale.product as g)
  {product: g.key, total: sum(g.items | ~.amount)}

// Multiple grouping keys
for (order in orders group by order.year, order.month as g)
  {year: g.key[0], month: g.key[1], revenue: sum(g.items | ~.total)}

// Count items per group
for (word in words group by word as g)
  {word: g.key, count: len(g.items)}

// Word frequency with sorting
for (word in document.words
     group by word as g
     order by len(g.items) desc
     limit 10)
  {word: g.key, frequency: len(g.items)}
```

#### Why Require `as`?

Unlike XQuery where iteration variables magically transform into sequences after grouping, Lambda requires explicit naming via `as` for clarity:

| Language | Key Access | Items Access |
|----------|------------|--------------|
| XQuery | Grouping variable | Original variable becomes sequence |
| C# LINQ | `group.Key` | `group` (IGrouping) |
| **Lambda** | `name.key` | `name.items` |

This explicit approach:
- Avoids confusion about variable semantics changing
- Makes group structure clear at the point of use
- Follows the same pattern as other languages (LINQ, Scala)

#### Future Consideration

A shorthand syntax using `~key` and `~items` may be added in the future:

```lambda
// Potential future syntax (not currently supported)
for (sale in sales group by sale.product)
  {product: ~key, total: sum(~items | ~.amount)}
```

---

### limit and offset Clauses

Control the number of results and pagination.

#### Syntax

```
limit <count>
offset <skip>
```

#### Semantics

- `limit N` — return at most N results
- `offset M` — skip the first M results
- Applied after filtering and sorting
- `limit` without `offset` starts from beginning
- `offset` without `limit` returns all remaining after skip

#### Examples

```lambda
// First 10 results
for (user in users order by user.created_at desc limit 10)
  user.name

// Pagination: page 3, 20 items per page
for (item in items order by item.id limit 20 offset 40)
  item

// Skip first 5
for (x in 1 to 100 offset 5 limit 10) x
// [6, 7, 8, 9, 10, 11, 12, 13, 14, 15]
```

---

### let Clause (within for)

The `let` clause introduces intermediate bindings within the iteration.

#### Syntax

```
for (<bindings>, let <name> = <expr>, ...)
```

Or as a separate clause (XQuery style):

```
for (<bindings> let <name> = <expr> where ...)
```

#### Semantics

- Computes a value once per iteration tuple
- Bound name available in subsequent clauses and body
- Multiple `let` bindings allowed
- Avoids redundant computation

#### Examples

```lambda
// Compute once, use multiple times
for (user in users, let fullName = user.firstName ++ " " ++ user.lastName
     where len(fullName) > 10
     order by fullName)
  {name: fullName, email: user.email}

// Multiple let bindings
for (order in orders,
     let subtotal = sum(order.items | ~.price * ~.qty),
     let tax = subtotal * 0.08,
     let total = subtotal + tax
     where total > 100)
  {id: order.id, subtotal: subtotal, tax: tax, total: total}

// Let with grouped data
for (sale in sales
     group by sale.category as g,
     let avgPrice = avg(g.items | ~.price))
  {category: g.key, avgPrice: avgPrice, count: len(g.items)}
```

---

## Clause Ordering and Semantics

### Logical Processing Order

Clauses are processed in a specific logical order, regardless of syntax position:

```
1. for (bindings)     — Generate iteration tuples
2. let               — Compute intermediate values
3. where             — Filter tuples
4. group by          — Aggregate into groups
5. let (post-group)  — Compute group-level values
6. order by          — Sort results
7. offset            — Skip initial results
8. limit             — Cap result count
9. body-expr         — Evaluate and collect
```

### Syntactic Order

The recommended syntactic order matches logical order:

```lambda
for (<bindings>
     [let <name> = <expr>, ...]
     [where <condition>]
     [group by <keys> as <name>]
     [order by <ordering>]
     [limit <n>]
     [offset <n>])
  <body-expr>
```

### Clause Interactions

| Clause | Available Variables | Produces |
|--------|---------------------|----------|
| `for` bindings | — | Iteration variables |
| `let` | All prior bindings | New binding |
| `where` | All bindings + lets | Filtered stream |
| `group by ... as g` | All bindings + lets | Group binding with `g.key`, `g.items` |
| `order by` | All bindings + group vars | Sorted stream |
| `limit`/`offset` | — | Truncated stream |

---

## Compatibility with Pipe Operator

The extended `for` expression is designed to complement, not replace, the pipe operator.

### When to Use Each

| Use Case | Recommended | Example |
|----------|-------------|---------|
| Simple transformation | Pipe | `data \| ~ * 2` |
| Simple filter | Pipe + where | `data where ~ > 0` |
| Complex iteration | For expression | `for (x in data where ...) ...` |
| Multiple bindings | For expression | `for (a in xs, b in ys) ...` |
| Grouping | For expression | `for (x in data group by ...)` |
| Chained transforms | Pipe | `data \| ~.a \| ~.b \| sum` |

### Interoperability

For expressions can appear in pipe chains:

```lambda
// For expression as pipe source
for (x in data where x > 0 order by x) x
  | ~ * 2
  | sum

// Pipe result as for source
data | ~.items
  | for (item in ~ where item.active) item.name
```

### Unified Filter Syntax

Both `for` and pipe use `where` for filtering:

```lambda
// Pipe with where
data where ~.active | ~.name

// For with where
for (item in data where item.active) item.name
```

The `where` clause in `for` operates on **bound variables**, while `where` in pipes operates on **`~` (current item)**.

### Converting Between Forms

Many expressions can be written either way:

```lambda
// Pipe style
users where ~.age >= 18 | ~.name | upper(~)

// For style
for (user in users where user.age >= 18) upper(user.name)

// Equivalent semantics
```

Choose based on readability and complexity.

---

## Examples

### Basic Filtering and Sorting

```lambda
// Active users sorted by name
for (user in users
     where user.active
     order by user.lastName, user.firstName)
  {name: user.firstName ++ " " ++ user.lastName, email: user.email}
```

### Pagination

```lambda
// Page 3 of search results (25 per page)
for (result in search_results
     order by result.relevance desc
     limit 25
     offset 50)
  {title: result.title, url: result.url}
```

### Grouping and Aggregation

```lambda
// Sales summary by region
for (sale in sales
     where sale.year == 2025
     group by sale.region as g
     order by sum(g.items | ~.amount) desc)
  {
    region: g.key,
    totalSales: sum(g.items | ~.amount),
    orderCount: len(g.items),
    avgOrder: avg(g.items | ~.amount)
  }
```

### Multiple Bindings with Filter

```lambda
// Find matching pairs
for (a in list1, b in list2
     where a.id == b.foreign_id and a.active)
  {item: a, related: b}
```

### Complex Data Processing

```lambda
// Top 5 categories by revenue with product counts
for (product in products,
     let revenue = product.price * product.sold
     where product.inStock
     group by product.category as g,
     let totalRevenue = sum(g.items | ~.revenue),
     let productCount = len(g.items)
     order by totalRevenue desc
     limit 5)
  {
    category: g.key,
    revenue: totalRevenue,
    products: productCount,
    topProduct: (g.items | ~ order by ~.revenue desc | first).name
  }
```

### Nested For Expressions

```lambda
// Flatten and process nested data
for (dept in departments,
     employee in dept.employees
     where employee.yearsOfService >= 5
     order by dept.name, employee.salary desc)
  {
    department: dept.name,
    employee: employee.name,
    salary: employee.salary
  }
```

### Integration with Elements

```lambda
// Transform HTML elements
for (row in table.rows
     where row.class contains "data"
     order by row.cells[0].text)
  <tr class: "processed";
    for (cell in row.cells limit 3)
      <td cell.text>
  >
```

---

## Grammar Changes

### Extended For Expression

```
for_expression
    : 'for' '(' for_clauses ')' expression
    ;

for_clauses
    : for_bindings for_let_clauses? where_clause? group_clause? order_clause? limit_clause? offset_clause?
    ;

for_bindings
    : for_binding (',' for_binding)*
    ;

for_binding
    : IDENTIFIER 'in' expression
    | IDENTIFIER ':' type_expr 'in' expression
    ;

for_let_clauses
    : (',' 'let' IDENTIFIER '=' expression)+
    | ('let' IDENTIFIER '=' expression)+
    ;

where_clause
    : 'where' expression
    ;

group_clause
    : 'group' 'by' group_keys 'as' IDENTIFIER
    ;

group_keys
    : expression (',' expression)*
    ;

order_clause
    : 'order' 'by' order_spec (',' order_spec)*
    ;

order_spec
    : expression order_direction?
    ;

order_direction
    : 'asc' | 'ascending' | 'desc' | 'descending'
    ;

limit_clause
    : 'limit' expression
    ;

offset_clause
    : 'offset' expression
    ;
```

### Extended For Statement

```
for_statement
    : 'for' for_bindings for_let_clauses? where_clause? group_clause? order_clause? limit_clause? offset_clause? block
    ;
```

### Keywords

New contextual keywords (only special within for clauses):

| Keyword | Purpose |
|---------|---------|
| `where` | Filter condition |
| `order` | Sort specification (with `by`) |
| `by` | Used with `order` and `group` |
| `group` | Grouping specification (with `by`) |
| `asc` / `ascending` | Ascending sort |
| `desc` / `descending` | Descending sort |
| `limit` | Maximum results |
| `offset` | Skip count |
| `as` | Alias for group items |

---

## Implementation Considerations

### Desugaring Strategy

For expressions with clauses desugar to core operations:

```lambda
// Source
for (x in data
     where x > 0
     order by x desc
     limit 10)
  x * 2

// Desugared
take(
  sort(
    for (x in data)
      if (x > 0) {__val: x, __sort: x} else null
    | filter(~ != null),
    (a, b) => b.__sort - a.__sort  // desc
  ),
  10
) | ~.__val * 2
```

### Group By Implementation

```lambda
// Source
for (x in data group by x.key as g) {key: g.key, sum: sum(g.items | ~.val)}

// Desugared
(let __groups = group_by(data, (x) => x.key),
 for (__g in __groups)
   (let g = __g,
    {key: g.key, sum: sum(g.items | ~.val)}))
```

### Evaluation Order

1. **Bindings** — iterate, generating tuples
2. **Let** — compute per-tuple bindings
3. **Where** — filter (short-circuit on false)
4. **Group** — collect into groups
5. **Post-group let** — compute group-level values
6. **Order** — sort complete result set
7. **Offset/Limit** — slice result set
8. **Body** — evaluate projection

### Lazy vs Eager Evaluation

| Clause | Evaluation |
|--------|------------|
| `where` | Lazy (filters as it iterates) |
| `order by` | Eager (must collect all to sort) |
| `group by` | Eager (must collect all to group) |
| `limit` | Lazy (can short-circuit after N) |
| `offset` | Semi-lazy (must count skipped) |

When `order by` or `group by` is present, iteration becomes eager. Without them, `limit` can enable early termination.

---

## Summary

### Quick Reference

| Clause | Purpose | Example |
|--------|---------|---------|
| `where` | Filter items | `where x > 0` |
| `order by` | Sort results | `order by x.name desc` |
| `group by` | Aggregate by key | `group by x.category as g` |
| `limit` | Max results | `limit 10` |
| `offset` | Skip first N | `offset 20` |
| `let` | Intermediate binding | `let sum = a + b` |

### Comparison with Other Languages

| Feature | Lambda | XQuery | SQL | LINQ | Scala |
|---------|--------|--------|-----|------|-------|
| Iteration | `for (x in ...)` | `for $x in ...` | `FROM` | `from x in` | `for (x <- ...)` |
| Binding | `let x = ...` | `let $x := ...` | — | `let x = ...` | `x = ...` |
| Filter | `where ...` | `where ...` | `WHERE` | `where ...` | `if ...` |
| Group | `group by ... as g` | `group by ...` | `GROUP BY` | `group ... by ... into g` | `.groupBy(...)` |
| Sort | `order by ...` | `order by ...` | `ORDER BY` | `orderby ...` | `.sortBy(...)` |
| Limit | `limit N` | `[position() <= N]` | `LIMIT N` | `.Take(N)` | `.take(N)` |
| Offset | `offset N` | — | `OFFSET N` | `.Skip(N)` | `.drop(N)` |
| Output | body expr | `return ...` | `SELECT` | `select ...` | `yield ...` |

### Design Principles

1. **Declarative** — Describe what you want, not how to compute it
2. **Composable** — Clauses combine cleanly
3. **Familiar** — Syntax resembles SQL/XQuery/LINQ
4. **Compatible** — Works alongside pipe operator
5. **Efficient** — Allows lazy evaluation where possible

This extension transforms Lambda's `for` expression into a powerful query language while maintaining its functional, expression-oriented nature.

---

## References

- [XQuery FLWOR Expressions](https://www.w3.org/TR/xquery-31/#id-flwor-expressions)
- [SQL SELECT Statement](https://www.postgresql.org/docs/current/sql-select.html)
- [C# LINQ Query Syntax](https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/concepts/linq/query-syntax-and-method-syntax-in-linq)
- [Scala for-comprehensions](https://docs.scala-lang.org/tour/for-comprehensions.html)
- [Haskell List Comprehensions](https://wiki.haskell.org/List_comprehension)
- [Python Generator Expressions](https://peps.python.org/pep-0289/)
