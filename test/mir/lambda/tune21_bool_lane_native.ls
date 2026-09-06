// T21-1c: an inferred bool array read publishes a native bool descriptor, so a
// condition branches on it directly, `and`/`or`/`not` stay native, and the
// out-of-bounds read still boxes to null (the lane's null is 2).
pn count_set(flags, n) {
    var c = 0
    var i = 0
    while (i < n) {
        if (flags[i]) { c = c + 1 }
        i = i + 1
    }
    return c
}
pn count_pairs(flags, n) {
    var c = 0
    var i = 0
    while (i + 1 < n) {
        if (flags[i] and not flags[i + 1]) { c = c + 1 }
        i = i + 1
    }
    return c
}
pn main() {
    var flags = fill(6, true)
    flags[1] = false
    flags[4] = false
    print(count_set(flags, 6)); print(" "); print(count_pairs(flags, 6)); print(" ")
    print(flags[9]); print(" "); print(flags[9] == null); print(" "); print(not flags[9]); print("\n")
}
