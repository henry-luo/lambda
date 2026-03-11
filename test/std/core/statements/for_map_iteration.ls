// Test: For Map Iteration
// Layer: 2 | Category: statement | Covers: for-in map iteration with keys

// ===== Keys only =====
[for (k, v in {a: 1, b: 2, c: 3}) k]

// ===== Key-value pairs =====
[for (k, v in {a: 1, b: 2, c: 3}) [k, v]]

// ===== Key-value concatenation =====
[for (k, v in {a: 1, b: 2, c: 3}) k ++ "=" ++ string(v)]

// ===== Values only =====
[for (k, v in {x: 10, y: 20}) v]

// ===== With where filter =====
[for (k, v in {a: 1, b: 5, c: 2} where v > 2) k]

// ===== With let clause =====
[for (k, v in {x: 3, y: 4}, let sq = v * v) [k, sq]]

// ===== Nested map properties =====
let person = {name: "Alice", age: 30, city: "NYC"}
[for (k, v in person) k]
[for (k, v in person) v]

// ===== Empty map =====
[for (k, v in {}) k]

// ===== Dynamic map =====
let m = map(["x", 10, "y", 20])
[for (k, v in m) k]
