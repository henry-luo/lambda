// Tune4 M1 fixture: declared numeric arrays need a runtime representation
// guard before inline loads/stores because another alias can still widen them.

pn guarded_load(a: float[], i: int) {
    return a[i]
}

pn guarded_store(a: float[], i: int) {
    a[i] = 9.5
    return a[i]
}

pn dense_int_sum(n: int) int {
    var values = fill(n * n, 1)
    var i: int = 0
    var total: int = 0
    while (i < n) {
        var j: int = 0
        while (j < n) {
            total = total + values[i * n + j]
            j = j + 1
        }
        i = i + 1
    }
    return total
}

pn dense_int_linear_sum(n: int) int {
    var values = fill(n, 1)
    var i: int = 0
    var total: int = 0
    while (i < n) {
        total = total + values[i]
        i = i + 1
    }
    return total
}

pn main() {
    var values = fill(4, 1.25)
    print(string(guarded_load(values, 2)) ++ "\n")
    print(string(guarded_store(values, 1)) ++ "\n")
    print(string(dense_int_sum(2)) ++ "\n")
    print(string(dense_int_linear_sum(2)) ++ "\n")
}
