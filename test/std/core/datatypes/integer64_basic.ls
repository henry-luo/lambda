// Test: Integer64 Basic
// Layer: 1 | Category: datatype | Covers: int64 type fundamentals

// ===== Int64 literals (L suffix) =====
0L
42L
-100L
9999999999L

// ===== Type checks =====
(42L is int64)
type(42L)

// ===== Int64 arithmetic =====
10L + 20L
100L - 50L
6L * 7L
42L / 6L

// ===== Large values =====
9999999999999L
-9999999999999L
1000000L * 1000000L

// ===== Int64 conversion =====
int64(42)
string(42L)

// ===== Int64 comparison =====
(42L == 42L)
(42L < 100L)
(100L > 42L)
