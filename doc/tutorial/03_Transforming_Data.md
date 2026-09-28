# 3. Transforming Data

Most scripts do three things with data: reshape every item, keep the items that matter, and boil the rest down to an answer. This chapter shows Lambda's tools for each — pipes that map and filter, `for` expressions with SQL-style clauses for sorting, grouping and joining, and arrays that compute like vectors. The examples read `books.json` and `sales.csv` from your working folder.

## Loading the Sample Data

`input` reads a file and turns it into Lambda values, choosing the parser by the file's extension. The `^` after the call passes the error on if the file cannot be read; [Chapter 4](04_Documents_as_Data.md) covers `input` in depth and [Chapter 6](06_Functions_and_Errors.md) explains `^`.

```text repl
λ> let books = input("books.json")^
null
λ> books[2]
{
  title: "Clean Code",
  author: "Robert Martin",
  year: 2008,
  price: 37.9,
  tags: ["craft"]
}
λ> let sales = input("sales.csv")^
null
λ> sales[0]
{
  date: "2026-01-05",
  region: "EMEA",
  product: "Widget",
  units: "12",
  price: "9.5"
}
```

JSON numbers arrive as numbers, but every CSV field arrives as a **string**: `units` is `"12"`, not `12`. Convert with `int(...)` or `float(...)` wherever you compute with a CSV field.

## Mapping with `|>`

The pipe `|>` evaluates its right side once for every item on its left and collects the results in an array. Inside the body, `~` is the current item and `~key` its index (or, for a map, its key). Save this as `pipes.ls`:

```lambda
// pipes.ls
let books = input("books.json")^
books |> ~.year;
books |> (~key + 1) ++ ". " ++ ~.author;
books |> ~.year |> sort;
books |> ~.price |> sum;
books |> ~.title |> take(2)
```

```bash
lambda pipes.ls
```

```text
[1999, 1985, 2008, 2017, 1968]
["1. Andrew Hunt", "2. Harold Abelson", "3. Robert Martin", "4. Martin Kleppmann", "5. Donald Knuth"]
[1968, 1985, 1999, 2008, 2017]
374.4
["The Pragmatic Programmer", "Structure and Interpretation of Computer Programs"]
```

- `books |> ~.year` projects one field out of every map. The body can be any expression; `++` turns the number `~key + 1` into text as it joins.
- A body that does not mention `~` receives the **whole** collection instead: `|> sort` is `sort(…)` of the years, `|> sum` adds up the prices, and `|> take(2)` passes the extra argument along, as `take(…, 2)`.
- Pipes chain left to right, so a long pipeline reads as a recipe, one step per stage.

## Filtering with `|:` and `that`

The filter stage `|:` keeps the items for which its body is true. It chains with `|>` like any other stage:

```lambda
// filters.ls
let books = input("books.json")^
books |: ~.price < 50 |> ~.title;
books |: "classic" in ~.tags |> ~.author;
[3, 8, 1, 9, 4] |: ~ > 3
```

```bash
lambda filters.ls
```

```text
["The Pragmatic Programmer", "Clean Code", "Designing Data-Intensive Applications"]
["Harold Abelson", "Donald Knuth"]
[8, 9, 4]
```

A `|:` body must mention `~`: write `xs |: is_even(~)`, not `xs |: is_even`. `x in xs` tests membership, so `"classic" in ~.tags` asks whether the tag list holds `"classic"`.

For a **single** value, `that` is the matching proviso: `x that p` is `x` when `p` holds for it and `null` otherwise. Inside the condition, a bare name that is not a variable reads a field of the value, so `year` means `~.year`:

```text repl
λ> let books = input("books.json")^
null
λ> books[2].price that ~ < 50
37.9
λ> (books[4].price that ~ < 50) or 50
50
λ> (books[2] that year > 2000).title
"Clean Code"
λ> (books[0] that year > 2000).title
null
```

A failed proviso is `null`, not an error, so `or` supplies a default. `that` never walks a collection: `books that len(~) > 3` is the whole array or `null`. To keep some of the items, use `|:`.

## `for` Expressions

A pipe covers one step. A `for` expression can do several at once, with clauses in the style of SQL: `where` filters, `order by` sorts (`desc` reverses it), `offset n` skips the first *n* results and `limit n` keeps at most *n* of the rest.

```lambda
// queries.ls
let books = input("books.json")^
for (b in books where b.year > 2000) b.year;
[for (b in books where b.price < 50) b.author];
[for (b in books order by b.price desc) b.price];
[for (b in books order by b.price desc limit 2) b.title];
[for (b in books order by b.title limit 2 offset 2) b.title]
```

```bash
lambda queries.ls
```

```text
2008
2017
["Andrew Hunt", "Robert Martin", "Martin Kleppmann"]
[190, 55, 49, 42.5, 37.9]
["The Art of Computer Programming", "Structure and Interpretation of Computer Programs"]
["Structure and Interpretation of Computer Programs", "The Art of Computer Programming"]
```

A `for` produces a **list**, and at the top level of a script a list spreads out, one item per line — which is why `2008` and `2017` print separately. Wrap the `for` in `[ … ]` to keep one array value. Do that whenever the items are strings: the top level of a script is content, like the children of an element, and adjacent strings there merge into one (S16.7).

A `let` clause names a value computed per item, for use in the later clauses and the body. Each clause may go on its own line inside the parentheses:

```lambda
// ages.ls
let books = input("books.json")^
for (b in books, let age = 2026 - b.year
     where age > 25
     order by age desc)
  {title: b.title, age: age}
```

```bash
lambda ages.ls
```

```text
{
  title: "The Art of Computer Programming",
  age: 58
}
{
  title: "Structure and Interpretation of Computer Programs",
  age: 41
}
{
  title: "The Pragmatic Programmer",
  age: 27
}
```

## Grouping

`group by key into g` partitions the rows by a key and runs the body once per group. The group `g` is an **element** with the tag `group`: the key is its attribute and the rows are its children. Print one to see:

```lambda
// groups.ls
let sales = input("sales.csv")^
let rows = sales |> {region: ~.region, units: int(~.units)}
for (r in rows group by r.region into g limit 1) g;
[for (r in rows group by r.region into g)
    g.region ++ ": " ++ len(content(g)) ++ " orders, " ++ sum(content(g) |> ~.units) ++ " units"]
```

```bash
lambda groups.ls
```

```text
<group region: "EMEA",
  {
    region: "EMEA",
    units: 12
  }
  {
    region: "EMEA",
    units: 3
  }>
["EMEA: 2 orders, 15 units", "APAC: 2 orders, 12 units", "AMER: 2 orders, 28 units"]
```

- `rows` first converts the CSV strings, so the groups hold numbers.
- `g.region` reads the key attribute. The attribute is named after the key's last field; for a computed key, name it with `as`: `group by len(w) as size into g`.
- `content(g)` is the array of the group's rows, so `len(content(g))` counts them and `content(g) |> ~.units` projects a column. Plain `len(g)` would also count the `region` attribute, as `len` does for every element.
- Groups come out in the order their keys first appear. After `group by`, the loop variable `r` is out of scope; `order by`, `limit` and the body see only `g`.

## Joining Sources

Two sources separated by a comma pair every item of the first with every item of the second. An `on` condition turns that into a **join**: only the pairs whose keys are equal go through. This script joins each sale to a small cost table to compute its margin:

```lambda
// margins.ls
let sales = input("sales.csv")^
let costs = [
    {product: "Widget", cost: 6.0},
    {product: "Gadget", cost: 15.5}
]
let margins = for (s in sales, c in costs on s.product == c.product)
    {date: s.date, product: s.product, margin: int(s.units) * (float(s.price) - c.cost)}
margins[0];
sum(margins |> ~.margin)
```

```bash
lambda margins.ls
```

```text
{
  date: "2026-01-05",
  product: "Widget",
  margin: 42
}
272.5
```

A sale with no matching cost would be dropped. Write `c? in costs` to keep it, with `c` bound to `null` — a left join.

## Arrays as Vectors

Arithmetic on arrays works item by item: an array and a number combine every item with the number, and two arrays of the same length combine item by item. Comparisons need care: `<` and `>` compare single values and `==` compares whole arrays, so element-wise comparison has its own operators, `eq`, `ne`, `lt`, `le`, `gt` and `ge`. Each returns a **mask**, an array of booleans that you count with `sum` or use as a subscript to pick the items where it is `true`.

```lambda
// vectors.ls
let sales = input("sales.csv")^
let units = sales |> int(~.units)
let price = sales |> float(~.price)
let region = sales |> ~.region
units * price;
price - 0.5;
units gt 7;
sum(units gt 7);
units[units gt 7];
region[units gt 7];
sum(units[region eq "EMEA"])
```

```bash
lambda vectors.ls
```

```text
[114, 66.5, 72, 190, 120, 192]
[9, 9, 23.5, 9, 23.5, 23.5]
[true, false, false, true, false, true]
3
[12, 20, 8]
["EMEA", "AMER", "AMER"]
15
```

`units * price` is the revenue of every sale in one step. A mask from one array can select from any other array of the same length: `region[units gt 7]` names the regions of the large orders. A mask is a container, and containers are always true, so reduce it with `all` or `any` before using it in an `if`.

## Aggregates

The collection functions round out the toolkit. `sum`, `avg`, `min` and `max` reduce an array to a number; `sort`, `unique` and `take` return a new array. [Lambda_Sys_Func.md](../Lambda_Sys_Func.md#collection-functions) lists them all.

```text repl
λ> let books = input("books.json")^
null
λ> let prices = books |> ~.price
null
λ> [sum(prices), avg(prices), min(prices), max(prices)]
[374.4, 74.88, 37.9, 190]
λ> sort(prices, 'desc')
[190, 55, 49, 42.5, 37.9]
λ> take(sort(prices, 'desc'), 3)
[190, 55, 49]
λ> let tags = [for (b in books) for (t in b.tags) t]
null
λ> unique(tags)
["craft", "career", "lisp", "classic", "data", "systems", "algorithms"]
```

- A float with no fractional part prints without one: the price `190.0` shows as `190`.
- The nested `for` yields every tag of every book, and nested `for` results flatten into one array. `unique` keeps the first occurrence of each value, in order.
- To sort records by a field, use `order by` in a `for`, as in the sections above.

## A Sales Report

Everything comes together in a short report: revenue and units per region, largest first, with a grand total. Save this as `sales_report.ls`:

```lambda
// sales_report.ls
let sales = input("sales.csv")^
let rows = sales |> {
    region: ~.region,
    units: int(~.units),
    revenue: int(~.units) * float(~.price)
}
let totals = for (r in rows group by r.region into g)
  {region: g.region,
   units: sum(content(g) |> ~.units),
   revenue: sum(content(g) |> ~.revenue)}
for (t in totals order by t.revenue desc) t
{region: "all", units: sum(rows |> ~.units), revenue: sum(rows |> ~.revenue)}
```

```bash
lambda sales_report.ls
```

```text
{
  region: "AMER",
  units: 28,
  revenue: 382
}
{
  region: "APAC",
  units: 12,
  revenue: 186.5
}
{
  region: "EMEA",
  units: 15,
  revenue: 186
}
{
  region: "all",
  units: 55,
  revenue: 754.5
}
```

The script reads as three steps: convert the CSV strings once, summarize each group, then sort the summaries. Because every step is a pure expression, you can print any intermediate value — `rows`, `totals` — to check it on its way.

## What You Learned

- `xs |> body` maps with `~` (the item) and `~key` (its index); a body without `~` receives the whole collection.
- `xs |: cond` filters a collection; `x that cond` keeps a single value or gives `null`.
- `for` expressions take `where`, `let`, `order by … desc`, `limit` and `offset` clauses; bracket a `for` to keep one array.
- `group by … into g` makes one `<group>` element per key, with the key as an attribute and the rows as children; `on` joins two sources.
- Arrays compute item by item; `eq ne lt le gt ge` make masks for `sum(mask)` and `arr[mask]`.
- CSV fields are strings until you convert them.

[Lambda_Expr_Stam.md](../Lambda_Expr_Stam.md) is the reference for [pipes](../Lambda_Expr_Stam.md#pipe-expressions), [for clauses](../Lambda_Expr_Stam.md#extended-for-expression-clauses) and [element-wise comparison](../Lambda_Expr_Stam.md#element-wise-comparison). Next, [Chapter 4](04_Documents_as_Data.md) applies the same tools to whole documents.
