// S4.1.2: finite facts belong only to the proved sibling and loop extent.
pn affine_sum() {
    var outer = 0
    var total = 0.0
    var values = fill(4, 0.0)
    while (outer < 4) {
        var k = 1
        while (k <= 2 + outer) { total = total + float(k); k = k + 1 }
        values[outer] = total
        outer = outer + 1
    }
    return values[3]
}
pn moving_bound(limit: int) {
    var k = 0
    var bound = limit
    var total = 0
    while (k < bound) {
        total = total + k
        bound = bound - 1
        k = k + 1
    }
    return total
}
pn start_counter(initial: int) {
    var k = initial
    var total = 0
    while (k < 4) { total = total + k; k = k + 1 }
    return total
}
pn main() {
    print([affine_sum(), moving_bound(6), moving_bound(0), moving_bound(-1)]); print("\n")
    print([start_counter(-2), start_counter(4), start_counter(9007199254740991 + 1)]); print("\n")
}
