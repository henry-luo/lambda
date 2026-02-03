// Test native comparison optimization for sys funcs
// When both operands have known numeric types, use native C comparisons
// instead of calling fn_lt, fn_le, fn_gt, fn_ge, fn_eq, fn_ne

// Section 1: Native int comparison optimizations
fn int_lt(a: int, b: int) { (a < b) }
fn int_le(a: int, b: int) { (a <= b) }
fn int_gt(a: int, b: int) { (a > b) }
fn int_ge(a: int, b: int) { (a >= b) }
fn int_eq(a: int, b: int) { (a == b) }
fn int_ne(a: int, b: int) { (a != b) }

"1. Int comparison functions"
[
    int_lt(1, 2),   // true
    int_lt(2, 1),   // false
    int_le(2, 2),   // true
    int_le(3, 2),   // false
    int_gt(3, 2),   // true
    int_gt(2, 3),   // false
    int_ge(2, 2),   // true
    int_ge(1, 2),   // false
    int_eq(5, 5),   // true
    int_eq(5, 6),   // false
    int_ne(5, 6),   // true
    int_ne(5, 5)    // false
]

// Section 2: Native float comparison optimizations
fn float_lt(a: float, b: float) { (a < b) }
fn float_le(a: float, b: float) { (a <= b) }
fn float_gt(a: float, b: float) { (a > b) }
fn float_ge(a: float, b: float) { (a >= b) }
fn float_eq(a: float, b: float) { (a == b) }
fn float_ne(a: float, b: float) { (a != b) }

"2. Float comparison functions"
[
    float_lt(1.0, 2.0),   // true
    float_le(2.0, 2.0),   // true
    float_gt(3.0, 2.0),   // true
    float_ge(2.0, 2.0),   // true
    float_eq(5.0, 5.0),   // true
    float_ne(5.0, 6.0)    // true
]

// Section 3: Bool equality (== and != use native, but < <= > >= should error)
fn bool_eq(a: bool, b: bool) { (a == b) }
fn bool_ne(a: bool, b: bool) { (a != b) }

"3. Bool equality (native)"
[
    bool_eq(true, true),    // true
    bool_eq(false, false),  // true
    bool_eq(true, false),   // false
    bool_ne(true, false),   // true
    bool_ne(false, false)   // false
]

// Section 4: Mixed numeric types (int/float) still use native
fn int_float_lt(a: int, b: float) { (a < b) }
fn float_int_gt(a: float, b: int) { (a > b) }

"4. Mixed int/float comparison (native)"
[
    int_float_lt(1, 2.5),   // true
    int_float_lt(3, 2.0),   // false
    float_int_gt(3.5, 2),   // true
    float_int_gt(1.5, 2)    // false
]

// Section 5: Untyped args fall back to fn_* (ensures runtime handles them)
fn untyped_lt(a, b) { (a < b) }
fn untyped_eq(a, b) { (a == b) }

"5. Untyped args (runtime fn_*)"
[
    untyped_lt(1, 2),       // true
    untyped_lt("a", "b"),   // true (string comparison)
    untyped_eq(5, 5),       // true
    untyped_eq("x", "x")    // true
]

// Section 6: Mixed typed/untyped falls back to fn_*
fn mixed_lt(a: int, b) { (a < b) }
fn mixed_eq(a: int, b) { (a == b) }

"6. Mixed typed/untyped (runtime fn_*)"
[
    mixed_lt(1, 2),     // true
    mixed_lt(3, 2),     // false
    mixed_eq(5, 5),     // true
    mixed_eq(5, 6)      // false
]

// Section 7: Comparisons in conditionals
fn max_int(a: int, b: int) { if (a > b) a else b }
fn min_int(a: int, b: int) { if (a < b) a else b }
fn is_between(x: int, lo: int, hi: int) { (x >= lo) and (x <= hi) }

"7. Comparisons in conditionals"
[
    max_int(5, 3),      // 5
    max_int(2, 8),      // 8
    min_int(5, 3),      // 3
    min_int(2, 8),      // 2
    is_between(5, 1, 10),   // true
    is_between(0, 1, 10),   // false
    is_between(15, 1, 10)   // false
]

// Section 8: Chained comparisons in recursive functions
fn count_down(n: int) {
    if (n <= 0) 0 else n + count_down(n - 1)
}

fn sum_range(lo: int, hi: int) {
    if (lo > hi) 0 else lo + sum_range(lo + 1, hi)
}

"8. Comparisons in recursive functions"
[
    count_down(5),      // 15 (5+4+3+2+1+0)
    sum_range(1, 5)     // 15 (1+2+3+4+5)
]

// Section 9: Complex expressions with multiple comparisons
fn classify_int(x: int) {
    if (x < 0) 'negative'
    else if (x == 0) 'zero'
    else if (x <= 10) 'small'
    else 'large'
}

"9. Complex expressions with comparisons"
[
    classify_int(-5),   // 'negative'
    classify_int(0),    // 'zero'
    classify_int(5),    // 'small'
    classify_int(100)   // 'large'
]

"All sys func optimization tests passed."
