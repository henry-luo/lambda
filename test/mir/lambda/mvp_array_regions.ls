// D4.3.3/S9.1.2: dense guards retain checked short/mixed arms and COW snapshots.
pn typed_sum(values: int[], limit: int) int^ {
    var total: int = 0
    var i: int = 0
    while (i < limit) { total = total + values[i]; i = i + 1 }
    return total
}
pn erased_sum(values, limit) any^ {
    var total = 0
    var i = 0
    while (i < limit) { total = total + values[i]; i = i + 1 }
    return total
}
pn typed_set(var values: int[], index: int, value: int) { values[index] = value }
pn erased_set(var values, index, value) { values[index] = value }
pn main() {
    var values: int[] = [1, 2, 3, 4]
    print([typed_sum(values, 4), erased_sum(values, 4)]); print("\n")
    print([typed_sum([7], 2) is error, erased_sum([1, "x"], 2) is error]); print("\n")
    let snapshot = values
    typed_set(values, 1, 9)
    erased_set(values, 2, 11)
    print([values[1], snapshot[1], values[2], snapshot[2]]); print("\n")
    var open_values = [1, 2]
    let open_snapshot = open_values
    erased_set(open_values, 0, "z")
    print([open_values[0], open_snapshot[0]]); print("\n")
}
