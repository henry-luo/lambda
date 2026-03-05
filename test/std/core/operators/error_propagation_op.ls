// Test: Error Propagation Operator
// Layer: 2 | Category: operator | Covers: ^ propagation, ^expr check

// ===== T^ function definition =====
fn fail() int^ {
    raise error("test error")
}

fn succeed() int^ {
    42
}

// ===== ^ propagation on calls =====
succeed()^

// ===== ^expr is_error check =====
fn test_is_error_on_error() {
    let a^err = fail()
    ^err
}
test_is_error_on_error()

fn test_is_error_on_success() {
    let b^err = succeed()
    ^err
}
test_is_error_on_success()

// ===== ^expr on plain values =====
^42
^"hello"
^null

// ===== Error is falsy =====
fn test_error_falsy() {
    let a^err = fail()
    if (err) 1 else 0
}
test_error_falsy()

// ===== Error or default =====
fn test_error_or_default() {
    let a^err = fail()
    err or 100
}
test_error_or_default()

// ===== let a^err destructuring =====
fn test_destructure_error() {
    let val^err = fail()
    [val == null, ^err, err.message]
}
test_destructure_error()

fn test_destructure_success() {
    let val^err = succeed()
    [val, ^err]
}
test_destructure_success()

// ===== Nested propagation =====
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a / b
}

fn compute(x) int^ {
    let doubled = divide(10, x)^
    doubled + 5
}
compute(2)^
