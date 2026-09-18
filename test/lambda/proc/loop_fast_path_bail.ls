// LR12-23: the interpreter's compact-int loop commits each assignment as it
// goes. When a value leaves the int53 band the fast path is abandoned
// mid-body, and the ordinary evaluator re-runs the whole iteration -- so every
// statement that had already committed ran twice. One iteration must be
// atomic: a bail restores the values the iteration started with (S4.1.2's
// saturation is unchanged; what changes is that it happens once).
pn count_with_bail(n: int) int {
    var c: int = 0
    var m: int = 1
    var i: int = 0
    while (i < n) {
        c = c + 1
        m = m * 9007199254740991
        i = i + 1
    }
    return c
}

pn sum_doubling(n: int) int {
    var s: int = 0
    var m: int = 1
    var i: int = 0
    while (i < n) {
        s = s + m
        m = m * 2
        i = i + 1
    }
    return s
}

// the bail happens inside an `if` arm, whose targets must roll back too
pn guarded_bail(n: int) int {
    var c: int = 0
    var m: int = 1
    var i: int = 0
    while (i < n) {
        c = c + 1
        if (i > 1) {
            m = m * 9007199254740991
            c = c + 10
        }
        i = i + 1
    }
    return c
}

pn main() {
    print("bail=" ++ count_with_bail(3) ++ " " ++ count_with_bail(1) ++ "\n")
    // n = 53 sums to exactly int53's maximum, which is a legal int
    print("double=" ++ sum_doubling(52) ++ " " ++ sum_doubling(53) ++ " " ++
        sum_doubling(54) ++ "\n")
    print("guarded=" ++ guarded_bail(4) ++ "\n")
}
