// Test: For Map Iteration
// Layer: 2 | Category: statement | Covers: for-at map iteration with keys

// ===== Keys only =====
[for (k at {a: 1, b: 2, c: 3}) k]

// ===== Key-value pairs =====
[for (k, v at {a: 1, b: 2, c: 3}) [k, v]]

// ===== Key-value concatenation =====
[for (k, v at {a: 1, b: 2, c: 3}) k ++ "=" ++ string(v)]

// ===== Values only =====
[for (k, v at {x: 10, y: 20}) v]

// ===== With where filter =====
[for (k, v at {a: 1, b: 5, c: 2} where v > 2) k]

// ===== With let clause =====
[for (k, v at {x: 3, y: 4}, let sq = v * v) [k, sq]]

// ===== Nested map properties =====
let person = {name: "Alice", age: 30, city: "NYC"}
[for (k, v at person) k]
[for (k, v at person) v]

// ===== Empty map =====
[for (k at {}) k]

// ===== Dynamic map =====
let m = map(["x", 10, "y", 20])
[for (k at m) k]
