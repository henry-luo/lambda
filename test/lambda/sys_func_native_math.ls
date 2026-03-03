// Test native math function optimization for sys funcs
// When argument has known numeric type, use native C math instead of runtime fn_*

// Section 1: Trigonometric functions with typed float
fn trig_sin(x: float) { math.sin(x) }
fn trig_cos(x: float) { math.cos(x) }
fn trig_tan(x: float) { math.tan(x) }

"1. Trigonometric functions (native)"
[
    trig_sin(0.0),          // 0
    trig_sin(1.5707963268), // ~1 (pi/2)
    trig_cos(0.0),          // 1
    trig_cos(3.1415926536), // ~-1 (pi)
    trig_tan(0.0)           // 0
]

// Section 2: Exponential and logarithmic functions
fn math_sqrt(x: float) { math.sqrt(x) }
fn math_log(x: float) { math.log(x) }
fn math_log10(x: float) { math.log10(x) }
fn math_exp(x: float) { math.exp(x) }

"2. Exponential and logarithmic (native)"
[
    math_sqrt(4.0),         // 2
    math_sqrt(9.0),         // 3
    math_log(2.718281828),  // ~1
    math_log10(100.0),      // 2
    math_exp(0.0),          // 1
    math_exp(1.0)           // ~2.718
]

// Section 3: Rounding functions
fn math_abs(x: float) { abs(x) }
fn math_floor(x: float) { floor(x) }
fn math_ceil(x: float) { ceil(x) }
fn math_round(x: float) { round(x) }

"3. Rounding functions (native)"
[
    math_abs(-5.5),     // 5.5
    math_abs(3.3),      // 3.3
    math_floor(3.7),    // 3
    math_floor(-3.7),   // -4
    math_ceil(3.2),     // 4
    math_ceil(-3.2),    // -3
    math_round(3.5),    // 4
    math_round(3.4)     // 3
]

// Section 4: Int argument (promoted to double)
fn int_sqrt(x: int) { math.sqrt(x) }
fn int_abs(x: int) { abs(x) }

"4. Int argument (promoted to double)"
[
    int_sqrt(16),       // 4
    int_abs(-10)        // 10
]

// Section 5: Untyped argument (falls back to runtime fn_*)
fn untyped_sqrt(x) { math.sqrt(x) }
fn untyped_sin(x) { math.sin(x) }

"5. Untyped argument (runtime fn_*)"
[
    untyped_sqrt(25),   // 5
    untyped_sin(0)      // 0
]

// Section 6: Chained math operations
fn hypotenuse(a: float, b: float) { math.sqrt(a*a + b*b) }
fn normalize_angle(x: float) { math.sin(x) / math.cos(x) }  // same as math.tan(x)

"6. Chained math operations"
[
    hypotenuse(3.0, 4.0),       // 5
    normalize_angle(0.0)        // 0
]

// Section 7: Math in conditionals
fn safe_sqrt(x: float) { 
    if (x < 0.0) 0.0 else math.sqrt(x) 
}

"7. Math in conditionals"
[
    safe_sqrt(9.0),     // 3
    safe_sqrt(-1.0)     // 0 (protected)
]

// Section 8: Math in recursive functions
fn factorial_approx(n: int) {
    // Stirling's approximation: n! ≈ math.sqrt(2*pi*n) * (n/e)^n
    if (n <= 1) 1.0
    else math.sqrt(6.2831853 * n) * math.exp(n * math.log(n) - n)
}

"8. Math in recursive/complex expressions"
[
    factorial_approx(5),    // ~120 (actual 120)
    factorial_approx(10)    // ~3628800 (actual 3628800)
]

"All native math optimization tests passed."
