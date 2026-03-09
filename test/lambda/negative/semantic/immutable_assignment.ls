// Negative test: assignment to immutable bindings
// Expected errors: E211 - cannot assign to let/parameter bindings

// Case 1: let reassignment in pn
pn test_let() {
    let x = 42
    x = 10
    print(x)
}

// Case 2: fn parameter reassignment (fn params are always immutable)
fn test_fn_param(y) {
    y = 99
    y
}

pn main() {
    test_let()
    test_fn_param(5)
}
