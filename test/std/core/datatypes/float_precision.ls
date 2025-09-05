// Test: Floating-Point Precision and Edge Cases
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test floating-point precision
let a = 0.1 + 0.2  
a
let b = 1.0 / 10.0  
b
let smallest = 5e-324  
smallest
let largest = 1.8e308  
largest
let denormal = 1e-323  
denormal
let nan = 0.0 / 0.0
nan
let is_nan = nan != nan  
is_nan
let inf = 1.0 / 0.0
inf
let neg_inf = -1.0 / 0.0
neg_inf
let is_inf = inf > 1.0e308  
is_inf
let pos_zero = 0.0
pos_zero
let neg_zero = -0.0
neg_zero
let zero_comp = pos_zero == neg_zero  
zero_comp
1
