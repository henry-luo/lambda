// Test: Floating-Point Comparison Operations
// Category: core/datatypes
// Type: positive
// Expected: 1

// Test equality with floating-point precision
let a = 0.1 + 0.2
a
let b = 0.3
b
let direct_eq = a == b
direct_eq
let epsilon = 1e-10
epsilon
let approx_eq = abs(a - b) < epsilon
approx_eq
let nan = 0.0 / 0.0
nan
let inf = 1.0 / 0.0
inf
let neg_inf = -1.0 / 0.0
neg_inf
let nan_eq = nan == nan
nan_eq
let nan_ne = nan != nan
nan_ne
let inf_gt = inf > 1e308
inf_gt
let neg_inf_lt = neg_inf < -1e308
neg_inf_lt
let float_int_comp = 1.0 == 1
float_int_comp
let float_str_comp = 1.0 == "1.0"
float_str_comp
1
