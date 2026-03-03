// Test new math functions: inverse trig, hyperbolic, exp/log variants, pow, cbrt

// Section 1: Inverse trigonometric functions
"1. Inverse trigonometric"
[
    math.asin(0),          // 0
    math.asin(1),          // pi/2 ≈ 1.5707963268
    math.acos(1),          // 0
    math.acos(0),          // pi/2 ≈ 1.5707963268
    math.atan(0),          // 0
    math.atan(1),          // pi/4 ≈ 0.7853981634
    math.atan2(1, 1),      // pi/4 ≈ 0.7853981634
    math.atan2(0, -1)      // pi ≈ 3.1415926536
]

// Section 2: Hyperbolic functions
"2. Hyperbolic"
[
    math.sinh(0),          // 0
    math.sinh(1),          // 1.1752011936
    math.cosh(0),          // 1
    math.cosh(1),          // 1.5430806348
    math.tanh(0),          // 0
    math.tanh(1)           // 0.7615941560
]

// Section 3: Inverse hyperbolic functions
"3. Inverse hyperbolic"
[
    math.asinh(0),         // 0
    math.asinh(1),         // 0.8813735870
    math.acosh(1),         // 0
    math.acosh(2),         // 1.3169578969
    math.atanh(0),         // 0
    math.atanh(0.5)        // 0.5493061443
]

// Section 4: Exponential/logarithmic variants
"4. Exp/log variants"
[
    math.exp2(0),          // 1
    math.exp2(3),          // 8
    math.exp2(0.5),        // sqrt(2) ≈ 1.4142135624
    math.expm1(0),         // 0
    math.expm1(1),         // e-1 ≈ 1.7182818285
    math.log2(1),          // 0
    math.log2(8),          // 3
    math.log2(0.5)         // -1
]

// Section 5: Power and cube root
"5. Power and cube root"
[
    math.pow(2, 3),        // 8
    math.pow(2, 0.5),      // sqrt(2) ≈ 1.4142135624
    math.pow(10, 0),       // 1
    math.cbrt(8),          // 2
    math.cbrt(27),         // 3
    math.cbrt(-8)          // -2
]

// Section 6: Vector element-wise operations
"6. Vector element-wise"
let v = [0.0, 0.5, 1.0]
math.asin(v)
math.sinh(v)
math.cbrt([1, 8, 27, 64])

// Section 7: Typed arguments (native optimization path)
fn typed_asin(x: float) { math.asin(x) }
fn typed_sinh(x: float) { math.sinh(x) }
fn typed_cbrt(x: float) { math.cbrt(x) }
fn typed_log2(x: float) { math.log2(x) }
fn typed_exp2(x: float) { math.exp2(x) }
fn typed_atan2(y: float, x: float) { math.atan2(y, x) }
fn typed_pow(a: float, b: float) { math.pow(a, b) }

"7. Typed arguments (native path)"
[
    typed_asin(0.5),
    typed_sinh(1.0),
    typed_cbrt(27.0),
    typed_log2(8.0),
    typed_exp2(3.0),
    typed_atan2(1.0, 1.0),
    typed_pow(2.0, 10.0)
]
