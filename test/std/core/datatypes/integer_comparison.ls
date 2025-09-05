// Test: Integer Comparison Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test comparison operators
let a = 5
let b = 10
let c = 5

// Equality and inequality
let eq1 = a == b  // false
let eq2 = a == c  // true
let neq1 = a != b // true
let neq2 = a != c // false

// Relational operators
let lt = a < b   // true
let gt = a > b   // false
let lte1 = a <= b // true
let lte2 = a <= c // true
let gte1 = a >= b // false
let gte2 = a >= c // true

// Test with different types
let str_comp = a == "5"  // false (type mismatch)
let bool_comp = a == true // false (1 == true)

// Test with null/undefined
let null_comp = a == null  // false

// Final check
1
