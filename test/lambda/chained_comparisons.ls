// Test chained comparisons (Python-style)

// Basic chained comparison
(1 < 2 < 3)              // expected: true (1 < 2 and 2 < 3)
(1 < 2 < 2)              // expected: false (2 < 2 is false)
(1 < 1 < 3)              // expected: false (1 < 1 is false)

// Range checks using chained comparisons
(5 < 7 < 10)             // expected: true
(5 < 5 < 10)             // expected: false
(5 < 10 < 10)            // expected: false

// Using in where clause
[1, 5, 7, 10, 15] where (5 < ~ < 10)   // expected: [7]

// Multiple items in range
[1, 2, 3, 4, 5, 6, 7, 8, 9, 10] where (3 < ~ < 8)  // expected: [4, 5, 6, 7]

// <= and >= style chains
(1 <= 1 <= 3)            // expected: true
(1 <= 2 <= 2)            // expected: true
(1 <= 3 <= 2)            // expected: false

// Mix of < and <=
(1 < 2 <= 2)             // expected: true (1 < 2 and 2 <= 2)
(1 <= 2 < 2)             // expected: false (2 < 2 is false)

// Combined result
[
  (1 < 2 < 3),           // true
  (5 < 7 < 10),          // true
  (5 < 5 < 10),          // false
  [1, 5, 7, 10, 15] where (5 < ~ < 10)  // [7]
]
