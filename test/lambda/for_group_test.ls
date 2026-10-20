// Test file for for-expression group by clauses (S14.1.1v2, S14.1.2v2)

"=== For Group Clauses ==="

let sales = [
  {region: "west", amount: 10},
  {region: "east", amount: 7},
  {region: "west", amount: 5}
];

// Single inferred key binds `region`; `g` is the array of member rows.
[for (x in sales group by x.region into g)
  {region: region, n: len(g), first: g[0].amount, total: sum(g |> ~.amount)}]

// Numeric-coherent keys; null keys form one group and its null rows stay members.
let xs = [1, 1.0, 2, null, null];
[for (x in xs group by x as value into g) {value: value, n: len(g)}]

// Multiple inferred keys with post-group ordering.
let orders = [
  {year: 2024, month: 2, amount: 1},
  {year: 2024, month: 1, amount: 3},
  {year: 2024, month: 2, amount: 4}
]
for (o in orders group by o.year, o.month into g order by month)
  {year: year, month: month, total: sum(g |> ~.amount)}

// A computed key takes an alias; string members stay separate rows.
let words = ["a", "bb", "cc", "ddd"]
for (w in words group by len(w) as wlen into g order by wlen)
  {length: wlen, n: len(g), members: g}

// Word count: grouping strings by themselves keeps every occurrence.
[for (w in ["to", "be", "or", "not", "to", "be"]
     group by w as word into g
     order by len(g) desc, word)
  {word: word, freq: len(g)}];

// The group binding is an array, one item per group.
[for (x in [3, 1, 3] group by x as v into g) g];
[for (x in [3, 1, 3] group by x as v into g) type(g)];

// After group by, order by / limit apply to groups.
[for (x in [5, 4, 5, 3, 4, 5] group by x as v into g order by len(g) desc limit 2) v];

// Row variables are out of scope after group by.
[for (x in [1, 2] group by x as value into g) x]

"All for group clause tests completed!"
