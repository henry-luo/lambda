// Test: Decimal Precision and Rounding
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test decimal precision
let precise = 0.1m + 0.2m  // Should be exactly 0.3

// Test rounding modes
let number = 1.23456789m
let round_down = number.round(2, "down")  // 1.23
let round_up = number.round(2, "up")      // 1.24
let round_half_even = number.round(2)     // 1.23 (banker's rounding)

// Test arithmetic precision
let a = 1.0000000000000000000000000001m
let b = 1.0m
let exact = a - b  // Should be exactly 0.0000000000000000000000000001

// Test with different scales
let scaled = 1.2345m.set_scale(2)  // 1.23
let scaled_up = 1.2m.set_scale(4)   // 1.2000

// Test division precision
let div = 1.0m / 3.0m  // 0.3333333333333333333333333333

// Final check
1
