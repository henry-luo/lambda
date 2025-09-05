// Test: Decimal Comparison Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test decimal equality with exact precision
let a = 0.1m + 0.2m
a
let b = 0.3m
b
let exact_eq = a == b
exact_eq
let price1 = 19.99m
price1
let price2 = 29.99m
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
let scaled1 = 1.0m
scaled1
let scaled2 = 1.00m
scaled2
let scaled_eq = scaled1 == scaled2
scaled_eq
let decimal_int = 1.0m == 1
decimal_int
let decimal_float = 1.0m == 1.0
decimal_float
let decimal_str = 1.0m == "1.0"
decimal_str
let zero = 0.0m
zero
let neg_zero = -0.0m
neg_zero
let zero_comp = zero == neg_zero
zero_comp
1
