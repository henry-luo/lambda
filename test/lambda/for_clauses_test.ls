// Test file for extended for-expression clauses (where, let, order by, limit, offset)

"=== Basic Clauses ==="

// Test 1: Basic for expression (existing functionality)
[for (x in [1, 2, 3]) x * 2]

// Test 2: for with WHERE clause
[for (x in [1, 2, 3, 4, 5] where x > 2) x]

// Test 3: for with LET clause
[for (x in [1, 2, 3], let y = x * 2) y + 1]

// Test 4: for with WHERE and LET
[for (x in [1, 2, 3, 4, 5], let doubled = x * 2 where doubled > 4) doubled]

// Test 5: for with LIMIT (no brackets - limit returns list)
for (x in [1, 2, 3, 4, 5] limit 3) x

// Test 6: for with OFFSET (no brackets - offset returns list)
for (x in [1, 2, 3, 4, 5] offset 2) x

// Test 7: for with LIMIT and OFFSET
for (x in [1, 2, 3, 4, 5, 6, 7] limit 3 offset 2) x

// Test 8: for with ORDER BY ascending (default)
for (x in [3, 1, 4, 1, 5] order by x) x

// Test 9: for with ORDER BY descending
for (x in [3, 1, 4, 1, 5] order by x desc) x

// Test 10: Combined - where, let, order by, limit, offset
for (x in [10, 5, 15, 20, 25, 3], let squared = x * x where x > 5 order by squared desc limit 2 offset 1) squared

// Test 11: for with float array
[for (x in [2.8, 1.2, 3.14]) x]

// Test 12: for with 'at' iteration (attribute/field iteration)
[for (k, v at {name: "Alice", age: 30}) k]

"=== Different Types ==="

// Test 13: String filtering with where
[for (s in ["apple", "banana", "apricot", "cherry"] where s[0] == "a") s]

// Test 14: String with let clause
[for (s in ["hello", "world"], let upper = s ++ "!" where len(s) > 4) upper]

// Test 15: Float array with where and order
for (x in [3.14, 1.41, 2.72, 0.58] where x > 1.0 order by x) x

// Test 16: Mixed operations on floats
[for (x in [1.5, 2.5, 3.5], let doubled = x * 2 where doubled > 4) doubled]

// Test 17: Map iteration with where clause
let people = [{name: "Alice", age: 30}, {name: "Bob", age: 25}, {name: "Carol", age: 35}]
[for (p in people where p.age > 28) p.name]

// Test 18: Map transformation with let
[for (p in people, let info = p.name ++ " (" ++ string(p.age) ++ ")") info]

// Test 19: Order by numeric field ascending
for (p in people order by p.age) p.age

// Test 20: Order by numeric field descending
for (p in people order by p.age desc) p.age

"=== Combined with Other Expressions ==="

// Test 21: Nested for with where
[for (row in [[1,2,3], [4,5,6], [7,8,9]]) for (x in row where x % 2 == 0) x]

// Test 22: For result in arithmetic
let sum_filtered = for (x in [1,2,3,4,5] where x > 2) x
sum(sum_filtered)

// Test 23: For with function call in body
[for (x in [1, 4, 9, 16] where x > 2) sqrt(x)]

// Test 24: For filtering only even numbers and doubling
[for (x in [1, 2, 3, 4, 5] where x % 2 == 0) x * 10]

// Test 25: Chained let clauses
[for (x in [1, 2, 3], let a = x + 1, let b = a * 2) b]

// Test 26: Let using previous let variable
[for (x in [2, 3, 4], let sq = x * x, let cube = sq * x where cube > 10) [x, sq, cube]]

// Test 27: For combined with spread
let base = [0]
let result = [base[0], for (x in [1, 2, 3] where x > 1) x * 10, 99]
result

// Test 28: Indexed iteration with clauses
[for (i, v in [10, 20, 30, 40, 50] where i > 1 limit 2) [i, v]]

"=== For Statement Tests ==="

// Test 29: For expression used as statement (result collected)
let result29 = for (x in [1, 2, 3, 4, 5] where x > 2) x * 2
result29

// Test 30: For expression with transform
let result30 = for (x in [1, 2, 3, 4, 5], let sq = x * x where sq > 5) sq
result30

// Test 31: For expression with nested structure filtering
let result31 = [for (p in people where p.age >= 30) p.name]
result31

"=== Negative and Corner Cases ==="

// Test 32: Empty array
[for (x in []) x]

// Test 33: Where filters all elements
[for (x in [1, 2, 3] where x > 100) x]

// Test 34: Limit greater than array length
for (x in [1, 2, 3] limit 10) x

// Test 36: Offset greater than array length
for (x in [1, 2, 3] offset 10) x

// Test 37: Offset equals array length
for (x in [1, 2, 3] offset 3) x

// Test 38: Single element array
[for (x in [42], let y = x * 2 where x > 0 order by y limit 1) y]

// Test 39: Order by with duplicate values
for (x in [3, 1, 3, 1, 2] order by x) x

// Test 40: Let with null handling
let items = [1, null, 3, null, 5]
[for (x in items where x != null) x]

// Test 41: Offset and limit edge case (offset + limit > length)
for (x in [1, 2, 3, 4, 5] limit 10 offset 3) x

// Test 42: Complex filter with multiple conditions (using and)
[for (x in 1 to 20 where x > 5 and x < 15 and x % 2 == 0) x]

// Test 43: Negative numbers in order by
for (x in [5, -3, 0, -7, 2] order by x) x

// Test 44: Float precision in where
[for (x in [0.1, 0.2, 0.3] where x > 0.15) x]

// Test 45: Empty result after all clauses
for (x in [1, 2, 3, 4, 5] where x > 10 order by x limit 2 offset 1) x

"All for clause tests completed!"
