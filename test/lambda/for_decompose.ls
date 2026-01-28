// Test for-expression and decomposition features
// Tests: indexed iteration, attribute iteration, spreading, decomposition

"=== Basic For Expression ==="

// Basic for-expression produces array
let arr = [1, 2, 3]
let doubled = for (v in arr) v * 2
doubled

// For-expression with range
let squares = for (x in 1 to 5) x * x
squares

"=== Indexed Iteration (for i, v in expr) ==="

// Index and value from array
let indexed = [for (i, v in [10, 20, 30]) [i, v]]
indexed

// Index and value from range
let range_indexed = [for (i, v in 5 to 7) i + v]
range_indexed

// Index with string array
let strs = ["a", "b", "c"]
let str_indexed = [for (i, s in strs) [i, s]]
str_indexed

"=== Attribute Iteration (for k, v at expr) ==="

// Key-value from map
let obj = {name: "Alice", age: 30, city: "NYC"}
let keys = [for (k, v at obj) k]
keys

let vals = [for (k, v at obj) v]
vals

// Key-value pairs
let pairs = [for (k, v at {x: 1, y: 2}) [k, v]]
pairs

"=== For Expression Spreading ==="

// Spread into array
let spread_arr = [0, for (v in [1, 2, 3]) v * 10, 99]
spread_arr

// Spread into list
let spread_list = (0, for (v in [1, 2]) v * 5, 99)
spread_list

// Multiple for-expressions spreading
let multi = [for (v in [1, 2]) v, for (v in [3, 4]) v * 10]
multi

// Nested with spreading
let nested = [for (row in [[1, 2], [3, 4]]) for (v in row) v * 2]
nested

"=== Positional Decomposition (let a, b = expr) ==="

// Decompose array
let a, b, c = [1, 2, 3]
[a, b, c]

// Decompose list
let x, y = (10, 20)
[x, y]

// Partial decomposition (fewer vars than elements)
let first, second = [100, 200, 300, 400]
[first, second]

// Decompose from nested array
let p, q = [[5, 6], [7, 8]][0]
[p, q]

"=== Named Decomposition (let a, b at expr) ==="

// Decompose map by field name
let person = {name: "Bob", age: 25, email: "bob@example.com"}
let name, age at person
[name, age]

// Different field order
let email, name at person
[email, name]

// Decompose computed map
let computed = {width: 100, height: 200}
let width, height at computed
width * height

"=== Combined Features ==="

// For with indexed iteration
let points = [[1, 2], [3, 4], [5, 6]]
let sums = for (i, pt in points) [i, pt]
sums

// Indexed iteration with spreading
let indexed_spread = [for (i, v in ["a", "b"]) [i, v]]
indexed_spread

// Attribute iteration with map
let attr_pairs = for (k, v at {a: 1, b: 2, c: 3}) [k, v]
attr_pairs

"=== Edge Cases ==="

// Empty array
let empty_for = for (v in []) v
empty_for

// Single element
let single = for (v in [42]) v * 2
single

// Decompose with expression
let d1, d2 = [1 + 1, 2 + 2]
[d1, d2]

"All for/decompose tests completed!"
