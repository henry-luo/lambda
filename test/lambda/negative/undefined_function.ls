// Negative test: calling undefined functions
// These should produce errors, not crashes

// Test 1: Typo in function name
fn make_adder(n: int) {
    fn add(x: int) => x + n;
    add
}

// Call with typo - should error, not crash
mke_adder(10)

// Test 2: Completely undefined function
unknown_func(1, 2, 3)

// Test 3: Undefined variable used as function
undefined_var(5)
