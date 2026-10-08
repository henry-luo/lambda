// Test: Mutation in Pure Function
// Layer: 2 | Category: negative | Covers: var/assignment/while inside fn

// var in fn - should produce compile error
fn bad_var() {
    var x = 10
    x
}
bad_var()

// while in fn - should produce compile error
fn bad_while() {
    while (true) { break }
}
bad_while()
