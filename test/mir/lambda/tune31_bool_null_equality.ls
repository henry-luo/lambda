// Tune31 T31-3: ordered comparisons can produce only Bool or null. Their
// untyped bindings retain Item storage, so equality compares canonical Items
// directly and preserves S5.1.1 / S6.1.2 for null pairs.

pn compare_neighbors(values, index) {
    var left = values[index] > 0
    var right = values[index + 1] > 0
    print((left != right) ++ " " ++ (left == right) ++ "\n")
}

pn main() {
    compare_neighbors([1, 2], 0)
    compare_neighbors([1, -1], 0)
    compare_neighbors([1], 0)
    compare_neighbors([1], 3)
}
