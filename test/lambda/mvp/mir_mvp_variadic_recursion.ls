// S16.9.7: tail calls evaluate and replace rest slots before the next body.
fn descend(n, ...) => if (n == 0) varg() else descend(n - 1, n, null)
fn empty(n, ...) => if (n == 0) varg() else empty(n - 1)
pn count_down(n, ...) {
    if (n == 0) { return varg() }
    return count_down(n - 1, n, "tail")
}
pn effect(value) {
    print(value)
    return value
}
pn side_effects(n, ...) {
    if (n == 0) { return varg() }
    return side_effects(n - 1, effect(n))
}
pn main() {
    let effects = side_effects(3, "initial")
    return [descend(3, "initial") == [1, null], empty(3, "initial") == [],
        count_down(3, "initial") == [1, "tail"], effects == [1]]
}
