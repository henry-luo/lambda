// Test: For Multi Variable
// Layer: 2 | Category: statement | Covers: for with multiple loop variables

// ===== Cross-product iteration =====
[for (x in [1, 2], y in [10, 20]) x + y]

// ===== Multiple ranges =====
[for (x in 1 to 3, y in 1 to 3 where x <= y) [x, y]]

// ===== String combinations =====
[for (prefix in ["a", "b"], suffix in ["1", "2"]) prefix ++ suffix]

// ===== With where =====
[for (x in 1 to 5, y in 1 to 5 where x * y == 12) [x, y]]

// ===== Three variables =====
[for (x in [1, 2], y in [3, 4], z in [5, 6]) x + y + z]
