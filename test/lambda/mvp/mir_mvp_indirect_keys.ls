// immutable key tables preserve bounds and COW; effectful keys freeze the receiver (S7.1.1v3, S9.1.2).
pn table_read(var values: bool[], key: int) any {
    let route = [2, 0, 1]
    let before = values[route[key]]
    values[2] = false
    return before
}
pn nested_table(values: any, key: int) any {
    let route = [2, 0, 1]
    let order = [1, 0]
    return values[route[order[key]]]
}
pn lazy_condition(values: bool[], key: int) int {
    let route = [1, 0]
    if (values[0] and values[route[key]]) { return 1 }
    return 0
}
pn mutable_table(values: int[], keys: int[]) any { return values[keys[0]] }
pn replacement_key(var values: int[]) int { values = fill(128, 9); return 0 }
// existing Full MIR reads the replacement; interpreter/AUTO/MVP retain the evaluated receiver.
pn effectful_read(var values: int[]) any { return values[replacement_key(values)] }
pn main() {
    var flags: bool[] = [true, false, true]
    let saved = flags
    let first = table_read(flags, 0)
    print([first, saved[2], flags[2]]); print("\n")
    print([table_read(flags, 1), table_read(flags, 2), table_read(flags, -1),
        table_read(flags, 3), table_read(flags, 4294967296)]); print("\n")
    print([nested_table([10, 20, 30], 0), nested_table([10, 20, 30], 1),
        nested_table([10, 20, 30], 2), nested_table([10, 20, 30], -1)]); print("\n")
    print([nested_table([1.5, 2.5, 3.5], 0), nested_table([true, false, false], 0),
        nested_table(["s", "t", "u"], 0), mutable_table([7, 8], [1])]); print("\n")
    var values: int[] = [7, 8]
    let before_replacement = effectful_read(values)
    print([before_replacement, values[0], len(values)]); print("\n")
    print([lazy_condition([false, true], 4294967296), lazy_condition([true, true], 0),
        lazy_condition([true, false], 0)]); print("\n")
}
