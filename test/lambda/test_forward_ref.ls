// Test forward function references
// Functions should be callable before they're defined

// Test 1: Simple forward reference
fn first() {
    second() + 10
}

fn second() {
    42
}

// Test 2: Mutual recursion
fn is_even(n) {
    if (n == 0) true else is_odd(n - 1)
}

fn is_odd(n) {
    if (n == 0) false else is_even(n - 1)
}

// Test 3: Chain of forward references
fn alpha() {
    beta() * 2
}

fn beta() {
    gamma() + 1
}

fn gamma() {
    5
}

// Test 4: Forward reference with multiple parameters
fn calculate(x, y) {
    helper(x, y) * 2
}

fn helper(a, b) {
    a + b
}

// Execute tests
[
    first(),
    second(),
    is_even(4),
    is_odd(4),
    is_even(5),
    is_odd(5),
    alpha(),
    beta(),
    gamma(),
    calculate(3, 4)
]
