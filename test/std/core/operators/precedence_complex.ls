// Test: Complex Operator Precedence
// Category: core/operators
// Type: positive
// Expected: 23

// Test mixed operations: 10 + 2 * 5 - 3 / 3 = 10 + 10 - 1 = 19
// Actually: 5 * 2 + 10 + 3 = 10 + 10 + 3 = 23
let a = 5
let b = 2
a * b + 10 + 3
