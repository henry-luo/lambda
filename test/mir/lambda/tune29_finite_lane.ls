// T29-3: a proven in-band operand skips its tests. A literal divisor needs no
// band test and no zero test; a comparison with a loop counter needs no nan
// test. A value from `div` keeps its test, and nan still compares unequal to
// itself while an infinite divisor still gives a poisoned or exact result
// (S4.1.2).
pn residues(n: int) int {
    var total: int = 0
    var i: int = 0
    while (i < n) {
        total = total + (i * 7) % 13 + i div 3
        i = i + 1
    }
    return total
}

pn counter_hits(values: int[], n: int) int {
    var hits: int = 0
    var i: int = 0
    while (i < n) {
        if (values[i] == i) { hits = hits + 1 }
        i = i + 1
    }
    return hits
}

pn quotient_eq(x: int, y: int, z: int) bool {
    return x div y == z
}

pn mod_by(x: int, y: int) int {
    return x % y
}

pn main() {
    print("residues=" ++ residues(100) ++ "\n")
    var values: int[] = [0, 5, 2, 3, 9]
    print("hits=" ++ counter_hits(values, 5) ++ "\n")
    let zero = 0
    let nan_val = zero div zero
    print("nan_self=" ++ (nan_val == nan_val) ++ " quot_nan=" ++ quotient_eq(0, 0, nan_val) ++
        " quot=" ++ quotient_eq(7, 2, 3) ++ "\n")
    print("mod_zero=" ++ mod_by(7, 0) ++ " mod_nan=" ++ mod_by(nan_val, 3) ++
        " mod_inf=" ++ mod_by(7, 1 div zero) ++ "\n")
}
