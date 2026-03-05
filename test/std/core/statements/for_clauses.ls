// Test: For Clauses
// Layer: 2 | Category: statement | Covers: where, let, order by, limit, offset

// ===== where clause =====
[for (x in [1, 2, 3, 4, 5] where x > 2) x]

// ===== let clause =====
[for (x in [1, 2, 3], let y = x * 2) y + 1]

// ===== where + let =====
[for (x in [1, 2, 3, 4, 5], let doubled = x * 2 where doubled > 4) doubled]

// ===== limit =====
for (x in [1, 2, 3, 4, 5] limit 3) x

// ===== offset =====
for (x in [1, 2, 3, 4, 5] offset 2) x

// ===== limit + offset =====
for (x in [1, 2, 3, 4, 5, 6, 7] limit 3 offset 2) x

// ===== order by ascending =====
for (x in [3, 1, 4, 1, 5] order by x) x

// ===== order by descending =====
for (x in [3, 1, 4, 1, 5] order by x desc) x

// ===== Combined =====
for (x in [10, 5, 15, 20, 25, 3], let sq = x * x where x > 5 order by sq desc limit 2 offset 1) sq

// ===== String with where =====
[for (s in ["apple", "banana", "apricot", "cherry"] where starts_with(s, "a")) s]

// ===== Map iteration with where =====
let people = [{name: "Alice", age: 30}, {name: "Bob", age: 25}, {name: "Carol", age: 35}]
[for (p in people where p.age > 28) p.name]

// ===== order by field =====
for (p in people order by p.age) p.age
for (p in people order by p.age desc) p.age

// ===== Nested for with where =====
[for (row in [[1,2,3], [4,5,6], [7,8,9]]) for (x in row where x % 2 == 0) x]
