// Test: Error Propagation Operator
// Layer: 2 | Category: operator | Covers: ^ propagation, is error check

// ===== T^ function definition =====
fn fail() int^ {
    raise error("test error")
}

fn succeed() int^ {
    42
}

// ===== ^ propagation on calls =====
succeed()^

// ===== is error check =====
fn test_is_error_on_error() {
    let a = fail() ^ { ^ }
    a is error
}
test_is_error_on_error()

fn test_is_error_on_success() {
    let b = succeed() ^ { ^ }
    b is error
}
test_is_error_on_success()

// ===== is error on plain values =====
42 is error
"hello" is error
null is error

// ===== Error is falsy =====
fn test_error_falsy() {
    let a = fail() ^ { ^ }
    if (a) 1 else 0
}
test_error_falsy()

// ===== Error or default =====
fn test_error_or_default() {
    let a = fail() ^ { ^ }
    a or 100
}
test_error_or_default()

// ===== handler-preserved error value =====
fn test_destructure_error() {
    let val = fail() ^ { ^ }
    [val == null, val is error, val.message]
}
test_destructure_error()

fn test_destructure_success() {
    let val = succeed() ^ { ^ }
    [val, val is error]
}
test_destructure_success()

// ===== Nested propagation =====
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a div b
}

fn compute(x) int^ {
    let doubled = divide(10, x)^
    doubled + 5
}
compute(2)^
