// S12.1.2v3: the former fn-local mutation negatives are now valid.

// a local var contributes no result of its own.
fn bad_var() {
    var x = 10
    x
}
bad_var()

// a statement loop contributes no result.
fn bad_while() {
    while (true) { break }
}
bad_while()
