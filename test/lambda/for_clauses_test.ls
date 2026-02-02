// Test file for extended for-expression clauses (where, let, order by, limit, offset)

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
