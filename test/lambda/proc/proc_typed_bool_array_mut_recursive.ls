pn set_three(rows, maxs, mins, value) {
    rows[0] = value
    maxs[0] = value
    mins[0] = value
}

pn read_three(rows, maxs, mins) int {
    if (rows[0] and maxs[0] and mins[0]) {
        return 1
    }
    return 0
}

pn recurse(rows: bool[], maxs, mins, depth: int) int {
    if (depth == 0) {
        return read_three(rows, maxs, mins)
    }
    set_three(rows, maxs, mins, false)
    return recurse(rows, maxs, mins, depth - 1)
}

pn main() {
    var rows: bool[] = [true]
    var maxs: bool[] = [true]
    var mins: bool[] = [true]
    print(recurse(rows, maxs, mins, 1))
    print(" ")
    rows[0] = true
    maxs[0] = true
    mins[0] = true
    print(if (rows[0] and maxs[0] and mins[0]) 1 else 0)
    print("\n")
}
