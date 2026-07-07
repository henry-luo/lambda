// Negative test: interior assignment through an immutable root binding
// Expected error: E211 - declare the root with var before mutating contents

pn main() {
    let xs = [1, 2, 3]
    xs[0] = 99
}
