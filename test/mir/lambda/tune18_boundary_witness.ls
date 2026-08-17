// Tune18 E1: the checked ArrayNum read keeps its nullable OOB edge while the
// in-bounds lane crosses a plain int declaration without a hot type check.

pn boundary18(values: int[], index: int) int {
    var picked: int = values[index]
    return picked
}

pn main() {
    print(boundary18(fill(4, 1), 0))
}
