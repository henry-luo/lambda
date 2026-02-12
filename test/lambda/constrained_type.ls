// Test constrained types with where clause

// Basic integer range constraint
type between5and10 = int where (5 < ~ < 10)
(6 is between5and10)     // expected: true
(7 is between5and10)     // expected: true
(9 is between5and10)     // expected: true
(5 is between5and10)     // expected: false (not > 5)
(10 is between5and10)    // expected: false (not < 10)
(3 is between5and10)     // expected: false
(15 is between5and10)    // expected: false

// Positive integer constraint
type positive = int where (~ > 0)
(1 is positive)          // expected: true
(100 is positive)        // expected: true
(0 is positive)          // expected: false
(-1 is positive)         // expected: false

// Constraint with <= and >=
type between1and10 = int where (~ >= 1 and ~ <= 10)
(1 is between1and10)     // expected: true
(5 is between1and10)     // expected: true
(10 is between1and10)    // expected: true
(0 is between1and10)     // expected: false
(11 is between1and10)    // expected: false

// Combined result
[
  (7 is between5and10),   // true
  (5 is between5and10),   // false
  (1 is positive),        // true
  (-1 is positive),       // false
  (1 is between1and10),   // true
  (11 is between1and10)   // false
]
