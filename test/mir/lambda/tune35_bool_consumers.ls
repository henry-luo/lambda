// D2.4.1, S5.1.1: native bool/null equality and checked bool call results.
pn compare_neighbors(values: float[], index: int) {
    var left = values[index] > 0.0
    var right = values[index + 1] > 0.0
    print([left != right, left == right]); print("\n")
}

pn positive(values: float[], index: int) bool {
    var value: float = values[index]
    return value > 0.0
}

pn classify(values: float[], index: int) int {
    if (positive(values, index)) { return 1 }
    return 0
}

pn main() {
    compare_neighbors([1.0, 2.0], 0)
    compare_neighbors([1.0, -1.0], 0)
    compare_neighbors([1.0], 0)
    compare_neighbors([1.0], 3)
    compare_neighbors([0.0 / 0.0, 1.0], 0)
    print([classify([1.0], 0), classify([-1.0], 0), type(positive([1.0], 4))])
    print("\n")
}
