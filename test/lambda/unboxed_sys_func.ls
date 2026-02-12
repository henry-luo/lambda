// Test unboxed system function optimizations
// Verifies that typed arguments use optimized unboxed function versions

// Section 1: Power operator with typed args
fn pow_float(x: float, y: float) { x ^ y }
fn pow_int(x: int, y: int) { x ^ y }

"1. Power operator (fn_pow_u)"
[
    pow_float(2.0, 3.0),    // 8
    pow_int(2, 10)          // 1024
]

// Section 2: min/max with two typed args
fn min_float(a: float, b: float) { min(a, b) }
fn max_float(a: float, b: float) { max(a, b) }
fn min_int(a: int, b: int) { min(a, b) }
fn max_int(a: int, b: int) { max(a, b) }

"2. min/max two-arg (fn_min2_u, fn_max2_u)"
[
    min_float(3.5, 2.5),    // 2.5
    max_float(3.5, 2.5),    // 3.5
    min_int(10, 20),        // 10
    max_int(10, 20)         // 20
]

// Section 3: Integer abs (fn_abs_i)
fn abs_int(x: int) { abs(x) }
fn abs_float(x: float) { abs(x) }

"3. abs with typed args"
[
    abs_int(-42),           // 42
    abs_float(-3.14)        // 3.14
]

// Section 4: sign with typed args (fn_sign_i, fn_sign_f)
fn sign_int(x: int) { sign(x) }
fn sign_float(x: float) { sign(x) }

"4. sign with typed args"
[
    sign_int(-100),         // -1
    sign_int(50),           // 1
    sign_int(0),            // 0
    sign_float(-2.5),       // -1
    sign_float(3.14)        // 1
]

// Section 5: floor/ceil/round with integers (should be identity - no function call)
fn floor_int(x: int) { floor(x) }
fn ceil_int(x: int) { ceil(x) }
fn round_int(x: int) { round(x) }

"5. floor/ceil/round with int (identity)"
[
    floor_int(42),          // 42
    ceil_int(42),           // 42
    round_int(42)           // 42
]

// Section 6: Verify typed comparison still works with unboxed math
fn cmp_with_abs(a: int, b: int) { (abs(a) < abs(b)) }
fn max_of_abs(a: int, b: int) { max(abs(a), abs(b)) }

"6. Compositions with unboxed"
[
    cmp_with_abs(-5, 10),       // true
    cmp_with_abs(-10, 5),       // false
    max_of_abs(-3, -7)          // 7
]
