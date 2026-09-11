// Tune26 T26-3: guarded bounded induction keeps square and stride counters
// in their finite native integer interval; the source path handles all others.

pn tune26_bounded_sieve(limit: int) int {
    var flags: bool[] = fill(limit + 1, true)
    flags[0] = false
    flags[1] = false
    var i: int = 2
    while (i * i <= limit) {
        if (flags[i]) {
            var j: int = i * i
            while (j <= limit) {
                flags[j] = false
                j = j + i
            }
        }
        i = i + 1
    }
    var count: int = 0
    i = 2
    while (i <= limit) {
        if (flags[i]) { count = count + 1 }
        i = i + 1
    }
    return count
}

pn tune26_unbounded_square_fallback() bool {
    // int division by zero is Lambda's saturated int value. It must select the
    // generic sibling, where its ordered comparison stays source-correct.
    var zero: int = 0
    var infinity: int = 1 div zero
    var i: int = infinity
    var limit: int = 10
    while (i * i <= limit) {
        i = i + 1
    }
    return i == infinity
}

pn main() {
    print(tune26_bounded_sieve(100)); print(" ")
    print(tune26_unbounded_square_fallback()); print("\n")
}
