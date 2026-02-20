// Test: Numeric Limits
// Layer: 2 | Category: boundary | Covers: int overflow, float special values

// Large integers
999999999
-999999999
1000000 * 1000000

// Float special values
1.0 / 0.0
-1.0 / 0.0
0.0 / 0.0
inf + 1
inf - inf
inf * 0
inf == inf
nan == nan
(inf > 1000000)

// Very small floats
1e-300
1e+300
