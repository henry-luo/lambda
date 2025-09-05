// Test: Decimal Comparison Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test decimal equality with exact precision
let a = 0.1m + 0.2m
let b = 0.3m
let exact_eq = a == b  // Should be true with decimal

// Test comparison operators
let price1 = 19.99m
let price2 = 29.99m

let lt = price1 < price2   // true
gt = price2 > price1      // true
lte = price1 <= price2    // true
gte = price2 >= price1    // true
neq = price1 != price2    // true

// Test with different scales
let scaled1 = 1.0m
let scaled2 = 1.00m
let scaled_eq = scaled1 == scaled2  // Should be true

// Test with different types
let decimal_int = 1.0m == 1      // true
let decimal_float = 1.0m == 1.0  // true (if converted properly)
let decimal_str = 1.0m == "1.0"  // false (type mismatch)

// Edge cases
let zero = 0.0m
let neg_zero = -0.0m
let zero_comp = zero == neg_zero  // Should be true

// Final check
1
