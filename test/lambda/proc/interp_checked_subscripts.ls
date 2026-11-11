// D8.3.1 / S9.1.2: computed coordinates use checked stores and keep snapshots.
pn coordinate(var order, value) {
    order = order * 10 + value
    return value
}

pn write_coords(var values, i, j, value) int^ {
    values[i, j] = value
    return 1
}

pn main() {
    var values = [[1, 2], [3, 4]]
    let saved = values
    var order = 0
    values[coordinate(order, 1), coordinate(order, 0)] = 9
    print([order, values[1, 0], saved[1, 0]]); print("\n")
    order = 0
    print([values[0, coordinate(order, 1)], order]); print("\n")
    write_coords(values, 0, 1.0, 8)^
    var rejected = false
    write_coords(values, 0, 1.5, 7) ^ { rejected = true }
    print([rejected, values[0, 1], saved[0, 1]]); print("\n")

    // S7.2.2: a nested read restores the write owner's `last` context.
    var flat = [1, 2, 3]
    let indices = [0]
    flat[indices[last] + last] = 6
    print(flat); print("\n")

    var record: map = {}
    record[string(1) ++ "x"] = 4
    let held = record
    record[symbol("1x")] = 5
    print([record["1x"], held["1x"]]); print("\n")
}
