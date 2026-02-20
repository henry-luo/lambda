// Negative test: assignment to immutable bindings
// Expected errors: E211 - cannot assign to let/parameter bindings

// Case 1: let reassignment in pn
pn test_let() {
    let x = 42
    x = 10
    print(x)
}

// Case 2: parameter reassignment in pn
pn test_param(y) {
    y = 99
    print(y)
}

pn main() {
    test_let()
    test_param(5)
}
