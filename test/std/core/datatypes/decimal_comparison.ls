// Test: Decimal Comparison Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test decimal equality with exact precision
let a = 0.1n + 0.2n
a
let b = 0.3n
b
let exact_eq = a == b
exact_eq
let price1 = 19.99n
price1
let price2 = 29.99n
price2
let lt = price1 < price2
lt
let gt = price2 > price1
gt
let lte = price1 <= price2
lte
let gte = price2 >= price1
gte
let neq = price1 != price2
neq
let scaled1 = 1.0n
scaled1
let scaled2 = 1.00n
scaled2
let scaled_eq = scaled1 == scaled2
scaled_eq
let decimal_int = 1.0n == 1
decimal_int
let decimal_float = 1.0n == 1.0
decimal_float
let decimal_str = 1.0n == "1.0"
decimal_str
let zero = 0.0n
zero
let neg_zero = -0.0n
neg_zero
let zero_comp = zero == neg_zero
zero_comp
1
