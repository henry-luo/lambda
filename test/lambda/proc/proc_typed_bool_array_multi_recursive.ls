pn recurse(rows: bool[], maxs: bool[], mins: bool[], depth: int) int {
    if (depth == 0) {
        if (rows[0] and maxs[1] and mins[0]) {
            return 1
        }
        return 0
    }
    return recurse(rows, maxs, mins, depth - 1)
}

pn main() {
    var rows: bool[] = [true, true]
    var maxs: bool[] = [true, true]
    var mins: bool[] = [true, true]
    print(recurse(rows, maxs, mins, 3))
    print(" ")
    maxs[1] = false
    print(recurse(rows, maxs, mins, 3))
    print("\n")
}
