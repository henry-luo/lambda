// Test error handling syntax: T^E (explicit error) and T^. (any error)

// ============================================
// 1. Basic function with T^. (any error)
// ============================================

fn may_fail(x) int^. {
    if (x < 0) raise error("negative input")
    else x * 2
}

// Test successful case
may_fail(5)
may_fail(0)

// ============================================
// 2. Function with explicit error type T^E
// ============================================

fn divide(a, b) int^error {
    if (b == 0) raise error("division by zero")
    else a / b
}

divide(10, 2)
divide(15, 3)

// ============================================
// 3. Function without error (plain return type)
// ============================================

fn safe_add(a, b) int {
    a + b
}

safe_add(3, 4)

// ============================================
// 4. Nested function calls with error types
// ============================================

fn compute(x) int^. {
    let doubled = may_fail(x)
    doubled + 10
}

compute(5)

// ============================================
// 5. Arrow function with error type
// ============================================

fn positive_sqrt(x) float^. => if (x < 0) raise error("negative") else x ^ 0.5

positive_sqrt(16)
positive_sqrt(25)

// ============================================
// 6. Multiple raise points
// ============================================

fn validate(x) int^. {
    if (x < 0) raise error("too small")
    else if (x > 100) raise error("too large")
    else x
}

validate(50)
validate(0)
validate(100)

// ============================================
// 7. Expression body with raise
// ============================================

fn require_even(n) int^. => if (n % 2 != 0) raise error("odd") else n

require_even(4)
require_even(10)

// ============================================
// 8. Type checking with error functions
// ============================================

type(may_fail)
type(divide)
type(safe_add)

// ============================================
// 9. Raise in let expression
// ============================================

fn check_bounds(x) int^. {
    let valid = if (x < 0 or x > 100) raise error("out of bounds") else x
    valid * 2
}

check_bounds(25)

// ============================================
// 10. Function returning error from nested call
// ============================================

fn double_validated(x) int^. {
    let v = validate(x)
    v * 2
}

double_validated(30)
