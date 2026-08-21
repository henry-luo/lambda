"=== For Join Group Clauses ==="

let orders = [
  {id: 1, cust_id: 1, amount: 10},
  {id: 2, cust_id: 1, amount: 5},
  {id: 3, cust_id: 2, amount: 7}
]

let customers = [
  {id: 1, name: "Ada"},
  {id: 2, name: "Ben"}
]

[for (o in orders, c in customers on o.cust_id == c.id
      group by c.name into g)
  {name: g.name, n: len(g)}]

"All for join group clause tests completed!"
