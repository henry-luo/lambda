// Test: Integer64 Basic
// Layer: 1 | Category: datatype | Covers: i64 type fundamentals

// ===== Int64 literals (i64 suffix) =====
0i64
42i64;
-100i64
9999999999i64;

// ===== Type checks =====
(42i64 is i64)
type(42i64)

// ===== Int64 arithmetic =====
10i64 + 20i64
100i64 - 50i64
6i64 * 7i64
42i64 / 6i64

// ===== Large values =====
9999999999999i64;
-9999999999999i64
1000000i64 * 1000000i64

// ===== Int64 conversion =====
i64(42)
string(42i64);

// ===== Int64 comparison =====
(42i64 == 42i64);
(42i64 < 100i64);
(100i64 > 42i64)
