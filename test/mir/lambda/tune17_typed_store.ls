// Tune17 T2: declared numeric and bool arrays retain their proven store lanes.

pn tune17_typed_store(flags: bool[], idx: int) {
    var values: int[] = fill(4, 0)
    values[0] = 1
    var tmp: int = values[0]
    values[1] = values[0]
    values[0] = values[1]
    values[1] = tmp

    flags[idx] = false
    flags[idx + 1] = true
    return 2
}

pn main() {
    var flags: bool[] = [true, true, true, true]
    tune17_typed_store(flags, 2)
    print(2)
    print("\n")
}
