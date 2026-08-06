// Negative test: an invisible cross-frame mutation cannot be observed by a
// later read. Propagate the new value explicitly or keep the read in `inner`.
// Expected error: E231 - assign the returned value back before reading it

pn main() {
    var xs = [1, 2, 3]
    pn inner() {
        xs[0] = 99
    }
    inner()
    xs
}
