// Tune30 T30-1: a counted `for` proves its whole index extent once at the
// loop head. The guard is `start >= 0` and `len >= end + 1` per root; every
// arm it cannot prove keeps the ordinary checked path, so an out-of-range
// read still yields null (S7.1.3v2) and a negative start still reads nothing
// raw.
// Counting rather than summing keeps every arm on one behaviour: an absent
// element compares false, where adding it to a declared `int` would raise
// E201 and expose the separate divergence ledgered as LR12-22.
pn count_range(a: int[], hi: int) int {
    var hits: int = 0
    for i in 0 to hi {
        if (a[i] > 0) {
            hits = hits + 1
        }
    }
    return hits
}

pn count_from(a: int[], lo: int, hi: int) int {
    var hits: int = 0
    for i in lo to hi {
        if (a[i] > 0) {
            hits = hits + 1
        }
    }
    return hits
}

pn fill_range(var a: int[], hi: int) any {
    for i in 0 to hi {
        a[i] = i * 2
    }
}

pn grid(a: int[], n: int) int {
    var hits: int = 0
    for r in 0 to n {
        for c in 0 to n {
            if (a[r] > 0 and a[c] > 0) {
                hits = hits + 1
            }
        }
    }
    return hits
}

pn main() {
    var a: int[] = [1, 2, 3, 4]
    print("in_range=" ++ string(count_range(a, 3)) ++ " " ++ string(count_range(a, 1)) ++ "\n")
    // the array is shorter than the range: the guard is false, reads are null
    print("short=" ++ string(count_range(a, 9)) ++ "\n")
    // a negative start keeps the checked path too
    print("neg=" ++ string(count_from(a, 0 - 2, 2)) ++ " " ++ string(count_from(a, 1, 2)) ++ "\n")
    // an empty range runs no iteration
    print("empty=" ++ string(count_range(a, 0 - 1)) ++ " " ++ string(count_from(a, 3, 1)) ++ "\n")
    var b: int[] = [0, 0, 0, 0]
    fill_range(b, 3)
    print("filled=" ++ string(b) ++ "\n")
    var c: int[] = [9, 9]
    fill_range(c, 5)
    print("short_store=" ++ string(c) ++ "\n")
    print("grid=" ++ string(grid(a, 1)) ++ " " ++ string(grid(a, 7)) ++ "\n")
    print("done\n")
}
