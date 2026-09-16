// Tune13 P0/P1: typed array stores should retain the native element lane when
// the RHS is an indexed arithmetic or type-preserving numeric builtin result.
// T28-7: indices are parameters so the checked cold store stays reachable.

pn tune13_store(key: int) int {
    var k: int = key
    var values: int[] = fill(3, 1)
    values[k] = values[k] + 4
    return values[k]
}

pn tune13_builtin(first: int, second: int) int {
    var a: int = first
    var b: int = second
    var values: int[] = fill(2, 2)
    values[a] = -4
    values[a] = abs(values[a])
    values[b] = min(values[b], 3)
    return values[a] + values[b]
}

pn main() {
    print(tune13_store(1))
    print("\n")
    print(tune13_builtin(0, 1))
    print("\n")
}
