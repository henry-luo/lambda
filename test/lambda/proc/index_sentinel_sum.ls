// Tune30: native index arithmetic must reject lane sentinels in its leaves.
// `inf + -inf` wrapped to 0 and `-inf + -inf` to 2 on the JIT, reading a[0]
// and a[2] where the interpreter yields null (S7.1.3v2, S4.1.2).
pn pick(a: int[], x: int, y: int) any {
    return a[x + y]
}

pn pick_diff(a: int[], x: int, y: int) any {
    return a[x - y]
}

pn pick_scaled(a: int[], x: int, y: int) any {
    return a[x * 2 + y]
}

pn main() {
    var a: int[] = [11, 22, 33]
    var big: int = 9007199254740991
    var pinf: int = big + 1
    var ninf: int = 0 - pinf
    print("pinf=" ++ pinf ++ " ninf=" ++ ninf ++ "\n")
    print("sum=" ++ (pinf + ninf) ++ "\n")
    print("pick=" ++ pick(a, pinf, ninf) ++ "\n")
    print("pick2=" ++ pick(a, ninf, ninf) ++ "\n")
    print("pick3=" ++ pick(a, pinf, pinf) ++ "\n")
    print("diff=" ++ pick_diff(a, pinf, pinf) ++ "\n")
    print("scaled=" ++ pick_scaled(a, ninf, 1) ++ "\n")
    // in-band extremes still cancel to a valid index
    print("edge=" ++ pick(a, big, 2 - big) ++ "\n")
    print("edge2=" ++ pick_diff(a, big, big - 1) ++ "\n")
    print("plain=" ++ pick(a, 1, 1) ++ " " ++ pick_scaled(a, 0, 1) ++ "\n")
}
