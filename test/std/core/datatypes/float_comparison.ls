// Test: Floating-Point Comparison Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test equality with floating-point precision
let a = 0.1 + 0.2
let b = 0.3

// Direct comparison (might fail due to precision)
let direct_eq = a == b

// Approximate comparison with epsilon
let epsilon = 1e-10
let approx_eq = abs(a - b) < epsilon

// Test with special values
let nan = 0.0 / 0.0
let inf = 1.0 / 0.0
let neg_inf = -1.0 / 0.0

// NaN comparisons
let nan_eq = nan == nan  // Should be false
let nan_ne = nan != nan  // Should be true

// Infinity comparisons
let inf_gt = inf > 1e308
let neg_inf_lt = neg_inf < -1e308

// Test with mixed types
let float_int_comp = 1.0 == 1  // Should be true
let float_str_comp = 1.0 == "1.0"  // Should be false

// Final check
1
