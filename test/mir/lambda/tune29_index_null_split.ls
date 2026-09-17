// T29-3 (D2.5.3): an int tree over a possibly out-of-range read tests the read
// once and branches to one null result. The result is still null end to end
// (S7.1.1v3), a nan still compares unequal to itself, and a nan or infinity
// operand still poisons the in-range result (S4.1.2).
pn step(seed: int[], i: int) int? {
    return (seed[i] * 3877 + 29573) % 139968
}

pn two_reads(a: int[], i: int, j: int) int? {
    return a[i] * 2 + a[j] - 1
}

pn nullable_var(x: int?, y: int) int? {
    return x * y + y * 2
}

pn eq_pair(a: int[], i: int, j: int) bool {
    let p: int = a[i]
    let q: int = a[j]
    return p == q
}

pn ne_pair(a: int[], i: int, j: int) bool {
    let p: int = a[i]
    let q: int = a[j]
    return p != q
}

pn main() {
    var seed: int[] = [42, 7]
    print("in=" ++ step(seed, 0) ++ " oob=" ++ (step(seed, 5) == null) ++
        " neg=" ++ (step(seed, -9) == null) ++ "\n")
    print("two=" ++ two_reads(seed, 0, 1) ++ " left_oob=" ++ (two_reads(seed, 9, 1) == null) ++
        " right_oob=" ++ (two_reads(seed, 0, 9) == null) ++ "\n")
    print("var=" ++ nullable_var(3, 4) ++ " null=" ++ (nullable_var(null, 4) == null) ++ "\n")
    let zero = 0
    var odd: int[] = [zero div zero, 1 div zero, -1 div zero, 5]
    print("nan_eq=" ++ eq_pair(odd, 0, 0) ++ " nan_ne=" ++ ne_pair(odd, 0, 0) ++
        " inf_eq=" ++ eq_pair(odd, 1, 1) ++ " inf_ne=" ++ ne_pair(odd, 1, 2) ++
        " val_eq=" ++ eq_pair(odd, 3, 3) ++ " mix=" ++ eq_pair(odd, 0, 3) ++ "\n")
    print("poison=" ++ step(odd, 0) ++ " " ++ step(odd, 1) ++ " " ++ two_reads(odd, 1, 2) ++ "\n")
}
