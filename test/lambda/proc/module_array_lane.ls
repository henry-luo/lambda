// T20-3. A module-level `let xs: T[]` publishes the generic ARRAY carrier, so
// every read of it used to take the boxed index helper and feed fn_add dispatch
// (5.4x on microdiff). The element witness now comes from the declared (or
// inferred) contract, paired with a runtime elem guard so a drifted
// representation falls back to the boxed read. These cases pin the witness, the
// guard's fallback, and the mutable exclusion.

let pl: int[] = [2, 6, 6, 7, 2, 3, 1, 1, 1, 2]
let fl: float[] = [1.5, 2.5, 3.5]
let ua = [10, 20, 30]

pn sum_ints() int {
    var s: int = 0
    var i: int = 0
    while (i < len(pl)) {
        s = s + pl[i]
        i = i + 1
    }
    return s
}

pn sum_floats() float {
    var s: float = 0.0
    var i: int = 0
    while (i < len(fl)) {
        s = s + fl[i]
        i = i + 1
    }
    return s
}

pn sum_unannotated() int {
    var s: int = 0
    var i: int = 0
    while (i < len(ua)) {
        s = s + ua[i]
        i = i + 1
    }
    return s
}

pn main() {
    print(sum_ints()) print(" ")
    print(sum_floats()) print(" ")
    print(sum_unannotated())
    print("\n")

    // dynamic index expression through the same witness
    var k = 3
    print(pl[k * 2]) print(" ") print(pl[k])
    print("\n")

    // out-of-bounds reads stay absence, not garbage (S7.1)
    print(pl[99]) print(" ") print(fl[99])
    print("\n")
}
