pn check(rows: bool[], maxs: bool[], mins: bool[], r: int, c: int) int {
    if (rows[r] and maxs[c + r] and mins[c - r + 7]) {
        return 1
    }
    return 0
}

pn main() {
    var rows: bool[] = fill(8, true)
    var maxs: bool[] = fill(16, true)
    var mins: bool[] = fill(16, true)
    print(check(rows, maxs, mins, 0, 0))
    print(" ")
    maxs[0] = false
    print(check(rows, maxs, mins, 0, 0))
    print("\n")
}
