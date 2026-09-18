// Tune30 §10.9: `int(...)`, `div` and `%` by a nonzero literal are admitted
// into an inlined leaf, which is what lets paraffins' ms2/ms3 and quicksort's
// lcg_next lose their call. An integer division by a nonzero literal has no
// error arm, and a saturated dividend still yields an int (S4.1.2).

pn tune30_ms2(r: int) int {
    return int(r * (r + 1) div 2)
}

pn tune30_lcg(seed: int) int {
    return int((seed * 1664525 + 1013904223) % 1000000)
}

pn tune30_walk(n: int) int {
    var total: int = 0
    var seed: int = 7
    var i: int = 0
    while (i < n) {
        total = total + tune30_ms2(i)
        seed = tune30_lcg(seed)
        i = i + 1
    }
    return total + seed
}

pn main() {
    print(tune30_walk(6)); print(" ")
    // in-band only: a saturated dividend makes T0 and the JIT disagree on
    // `int(inf div 2)`, which is LR12-23's family and not what this pins
    print(tune30_ms2(11)); print(" ")
    print(tune30_lcg(0 - 5)); print("\n")
}
