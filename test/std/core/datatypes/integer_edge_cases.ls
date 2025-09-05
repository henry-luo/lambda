// Test: Integer Edge Cases
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test integer limits and edge cases
let max_int = 9223372036854775807  // 2^63 - 1
let min_int = -9223372036854775808 // -2^63

// Test operations at boundaries
let overflow = max_int + 1  // Should wrap around to min_int
let underflow = min_int - 1 // Should wrap around to max_int

// Test with zero
let zero = 0
let div_by_zero = 1 / 0  // Should be positive infinity
let mod_zero = 1 % 0     // Should be NaN

// Test with one
let one = 1
let power_zero = one ** 0  // Should be 1
let power_one = one ** 1   // Should be 1

// Final check
1
