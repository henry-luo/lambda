// Negative test: closures may read captured snapshots, but cannot mutate them
// Expected error: E211 - pass mutable state as a var parameter or return a value

pn main() {
    var xs = [1, 2, 3]
    pn inner() {
        xs[0] = 99
    }
    inner()
}
