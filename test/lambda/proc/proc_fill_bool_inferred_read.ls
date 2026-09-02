// An inferred var parameter must read fill(..., bool)'s packed ELEM_BOOL lane
// byte by byte and keep inferred logical operands at their boxed truthiness
// boundary rather than forcing them into a native bool representation.

pn count_enabled(var flags, n) {
    var count = 0
    var i = 0
    while (i < n) {
        if (flags[i]) {
            count = count + 1
        }
        i = i + 1
    }
    return count
}

pn count_adjacent(var flags, n) {
    var count = 0
    var i = 0
    while (i + 1 < n) {
        if (flags[i] and flags[i + 1]) {
            count = count + 1
        }
        if ((not flags[i]) and flags[i + 1]) {
            count = count + 10
        }
        i = i + 1
    }
    return count
}

pn main() {
    var flags = fill(8, true)
    flags[1] = false
    flags[5] = false
    print(count_enabled(flags, 8))
    print("\n")
    print(count_adjacent(flags, 8))
    print("\n")
}
