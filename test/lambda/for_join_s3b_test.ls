// Test file for S3b join pipeline completion:
// index/key-preserving joins and mixed join/cross-product source lists.

"=== For Join S3b ==="

let orders = [
  {cid: 1, item: "a"},
  {cid: 2, item: "b"},
  {cid: 1, item: "c"}
]

let customers = [
  {id: 1, name: "Ann"},
  {id: 2, name: "Bob"}
]

// Index on the first (probe) source survives the join, preserving prior order.
[for (i, o in orders, c in customers on o.cid == c.id)
  {pos: i, item: o.item, name: c.name}]

// Index on the joined (new) source reports the matched row's position.
[for (o in orders, j, c in customers on o.cid == c.id)
  {item: o.item, cpos: j, name: c.name}]

// Index survives a left join; the null-padded row also nulls the index.
let orders2 = [{cid: 1, item: "a"}, {cid: 9, item: "z"}]
[for (i, o in orders2, c? in customers on o.cid == c.id)
  {pos: i, item: o.item, name: if (c != null) c.name else "none"}]

// Mixed join + cross-product: each joined tuple crosses with every tag.
let tags = ["x", "y"]
[for (o in orders, c in customers on o.cid == c.id, t in tags)
  {item: o.item, name: c.name, tag: t}]

// Cross-product source BEFORE a join source; the join key still resolves.
[for (o in orders, t in tags, c in customers on o.cid == c.id)
  {item: o.item, tag: t, name: c.name}]

// Key-only source in a join: the key binds as the value (symbol keys match symbol fields).
let caps = {a: 100, b: 200}
let picks = [{item: 'a'}, {item: 'b'}, {item: 'a'}]
[for (p in picks, k at caps on p.item == k)
  {item: p.item, key: k}]

"All for join S3b tests completed!"
