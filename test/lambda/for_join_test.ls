// Test file for for-expression join on clauses

"=== For Join Clauses ==="

let orders = [
  {id: 101, cust_id: 1, region_id: "r1", total: 50},
  {id: 102, cust_id: 2, region_id: "r2", total: 25},
  {id: 103, cust_id: 3, region_id: "r9", total: 99},
  {id: 104, cust_id: null, region_id: "r0", total: 5},
  {id: 105, cust_id: 1.0, region_id: "r1", total: 7}
]

let customers = [
  {id: 1, name: "Ada", region_id: "r1"},
  {id: 1, name: "Ada-alt", region_id: "r1"},
  {id: 2, name: "Ben", region_id: "r2"},
  {id: null, name: "Null Customer", region_id: "r0"}
]

// Inner join preserves prior order and duplicate matches preserve new-source order.
[for (o in orders, c in customers on o.cust_id == c.id)
  {order: o.id, name: c.name, total: o.total}]

// Left join pads missing matches with null; null join keys never match.
[for (o in orders, c? in customers on o.cust_id == c.id)
  {order: o.id, name: if (c.name) c.name else "unknown"}]

let regions = [
  {id: "r1", label: "West"},
  {id: "r2", label: "East"},
  {id: "r9", label: "No Customer Region"}
]

// Chained multi-key join: the new source must match both prior bindings.
[for (o in orders,
      c in customers on o.cust_id == c.id,
      r in regions on c.region_id == r.id and o.region_id == r.id)
  {order: o.id, name: c.name, region: r.label}]

"All for join clause tests completed!"
