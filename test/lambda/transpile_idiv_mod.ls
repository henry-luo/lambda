// Test integer division (div) and modulo (%) with typed parameters
// Verifies native fn_mod_i / fn_idiv_i dispatch from Phase 5

// ============================================
// Section 1: Basic div and mod
// ============================================

"1. Basic div and mod"
[
    10 div 3,           // 3
    17 % 5,             // 2
    -10 div 3,          // -3
    -17 % 5,            // -2
    100 div 7,          // 14
    100 % 7             // 2
]

// ============================================
// Section 2: Typed int div (native fn_idiv_i)
// ============================================

fn int_div(a: int, b: int) { a div b }
fn int_mod(a: int, b: int) { a % b }

"2. Typed int div/mod"
[
    int_div(10, 3),     // 3
    int_div(17, 5),     // 3
    int_div(-10, 3),    // -3
    int_div(100, 7),    // 14
    int_mod(10, 3),     // 1
    int_mod(17, 5),     // 2
    int_mod(-10, 3),    // -1
    int_mod(100, 7)     // 2
]

// ============================================
// Section 3: Float mod (fmod path)
// ============================================

fn float_mod(a: float, b: float) { a % b }

"3. Float mod"
[
    float_mod(10.5, 3.0),  // 1.5
    float_mod(7.0, 2.5)    // 2
]

// ============================================
// Section 4: Typed div/mod in expressions
// ============================================

fn is_even(n: int) { n % 2 == 0 }
fn is_odd(n: int) { n % 2 != 0 }
fn quotient_remainder(a: int, b: int) { [a div b, a % b] }

"4. Div/mod in expressions"
[
    is_even(10),         // true
    is_even(7),          // false
    is_odd(7),           // true
    is_odd(10)           // false
]
quotient_remainder(17, 5)

// ============================================
// Section 5: Nested div/mod
// ============================================

fn digit_sum(n: int) {
    n % 10 + (n div 10) % 10 + (n div 100) % 10
}

"5. Digit sum via div/mod"
[
    digit_sum(123),     // 6
    digit_sum(999),     // 27
    digit_sum(100)      // 1
]

// ============================================
// Section 6: Div/mod with variables
// ============================================

"6. Div/mod with variables"
let x = 42
let y = 10
[x div y, x % y]

// ============================================
// Section 7: Mixed typed/untyped
// ============================================

fn untyped_div(a, b) { a div b }
fn untyped_mod(a, b) { a % b }

"7. Untyped div/mod"
[untyped_div(17, 5), untyped_mod(17, 5)]
