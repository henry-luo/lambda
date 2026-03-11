// Test: 'in' operator for all container types (value membership)
// Verifies: map, vmap, element check values (not keys), plus array[int], array[float], range, list, string

"=== Map (value membership) ==="
let m = {name: "Alice", age: 30}
"Alice" in m
30 in m
"Bob" in m
"name" in m

"=== VMap (value membership) ==="
let vm = map(["x", 100, "y", 200])
100 in vm
200 in vm
300 in vm
"x" in vm

"=== Element (attr values + children) ==="
let doc^err = parse("<item x='1' y='2'>hello</item>", "xml")
let el = doc[0]
"1" in el
"hello" in el
"missing" in el

"=== Array ==="
let arr = [10, 20, 30]
20 in arr
40 in arr

"=== Array[int] ==="
2 in [1, 2, 3]
4 in [1, 2, 3]

"=== Array[float] ==="
let af = [1.5, 2.5, 3.5]
2.5 in af
4.5 in af

"=== Range ==="
3 in 1 to 5
6 in 1 to 5

"=== List ==="
"b" in ("a", "b", "c")
"d" in ("a", "b", "c")

"=== String substring ==="
"ell" in "hello"
"xyz" in "hello"
