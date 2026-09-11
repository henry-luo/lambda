// Tune26 T26-4: a local bool[] keeps its certified unique carrier through
// same-lane stores, while an alias still takes the COW-safe checked path.

pn tune26_same_owner_store(n: int) int {
    var flags: bool[] = fill(n + 1, true)
    flags[0] = false
    var i: int = 2
    var count: int = 0
    while (i <= n) {
        flags[i] = false
        if (not flags[i]) { count = count + 1 }
        i = i + 1
    }
    return count
}

pn tune26_same_owner_snapshot() bool {
    var flags: bool[] = fill(4, true)
    let snapshot = flags
    var i: int = 0
    while (i < 4) {
        flags[i] = false
        i = i + 1
    }
    return snapshot[0]
}

pn main() {
    print(tune26_same_owner_store(7)); print(" ")
    print(tune26_same_owner_snapshot()); print("\n")
}
