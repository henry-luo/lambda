// Test file for for-expression group by clauses

"=== For Group Clauses ==="

let sales = [
  {region: "west", amount: 10},
  {region: "east", amount: 7},
  {region: "west", amount: 5}
]

// Single inferred key: group attr is g.region; members are children.
[for (x in sales group by x.region into g)
  {region: g.region, n: len(g), first: g[0].amount, total: sum(g |> ~["amount"])}]

// Numeric tower coherence and null grouping.
let xs = [1, 1.0, 2, null, null]
[for (x in xs group by x as value into g) {value: g.value, n: len(g)}]

// Multiple inferred keys with post-group ordering.
let orders = [
  {year: 2024, month: 2, amount: 1},
  {year: 2024, month: 1, amount: 3},
  {year: 2024, month: 2, amount: 4}
]
for (o in orders group by o.year, o.month into g order by g.month)
  {year: g.year, month: g.month, total: sum(g |> ~["amount"])}

// Computed keys require an explicit alias.
let words = ["a", "bb", "cc", "ddd"]
for (w in words group by len(w) as wlen into g order by g.wlen)
  {length: g.wlen, n: len(g), first: g[0]}

// Row variables are out of scope after group by.
[for (x in [1, 2] group by x as value into g) x]

"All for group clause tests completed!"
