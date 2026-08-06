// Tune13 P0/P1: typed array stores should retain the native element lane when
// the RHS is an indexed arithmetic or type-preserving numeric builtin result.

pn tune13_store() int {
    var values: int[] = fill(3, 1)
    values[1] = values[1] + 4
    return values[1]
}

pn tune13_builtin() int {
    var values: int[] = fill(2, 2)
    values[0] = -4
    values[0] = abs(values[0])
    values[1] = min(values[1], 3)
    return values[0] + values[1]
}

pn main() {
    print(tune13_store())
    print("\n")
    print(tune13_builtin())
    print("\n")
}
