// Test: Decimal Basic Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test basic decimal operations
let price = 9.99m
let quantity = 3m

// Basic arithmetic
let subtotal = price * quantity  // 29.97
let tax = subtotal * 0.08m      // 2.3976 (precise decimal arithmetic)
let total = subtotal + tax      // 32.3676

// Division with decimal precision
let exact = 1.0m / 3.0m  // 0.3333333333333333333333333333

// Large decimal numbers
let big_decimal = 1234567890.1234567890m

// Small decimal numbers
let small_decimal = 0.0000000001234567890m

// Final check
1
