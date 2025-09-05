// Test: Decimal Precision and Rounding
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test decimal precision
let precise = 0.1m + 0.2m
precise
let number = 1.23456789m
number
let round_down = number.round(2, "down")
round_down
let round_up = number.round(2, "up")
round_up
let round_half_even = number.round(2)
round_half_even
let a = 1.0000000000000000000000000001m
a
let b = 1.0m
b
let exact = a - b
exact
let scaled = 1.2345m.set_scale(2)
scaled
let scaled_up = 1.2m.set_scale(4)
scaled_up
let div = 1.0m / 3.0m
div
1
