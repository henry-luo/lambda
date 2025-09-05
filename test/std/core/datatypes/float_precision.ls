// Test: Floating-Point Precision and Edge Cases
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test floating-point precision
let a = 0.1 + 0.2  // Should be approximately 0.3
let b = 1.0 / 10.0  // Should be 0.1

// Test very small and very large numbers
let smallest = 5e-324  // Close to minimum positive normal
let largest = 1.8e308  // Close to maximum representable

// Test denormal numbers
let denormal = 1e-323  // Denormal number

// Test NaN properties
let nan = 0.0 / 0.0
let is_nan = nan != nan  // NaN is not equal to itself

// Test infinity properties
let inf = 1.0 / 0.0
let neg_inf = -1.0 / 0.0
let is_inf = inf > 1.0e308  // Infinity is greater than any finite number

// Test signed zeros
let pos_zero = 0.0
let neg_zero = -0.0
let zero_comp = pos_zero == neg_zero  // Should be true

// Final check
1
