// Tune30 (S7.1.3v2): a typed-array loop whose index is proven below its bound
// still reads null when the array is shorter than that bound, and a declared
// binding still rejects that null. The JIT's dense loop guard compared a
// literal bound as -1 (always true) and read past the end, and its fallback
// arm published the nullable result as proven, so `s = s + a[i]` gave nan
// instead of the E201 the interpreter raises.
pn sum_lit(a: float[]) float {
    var s: float = 0.0
    var i: int = 0
    while (i < 5) {
        s = s + a[i]
        i = i + 1
    }
    return s
}

pn sum_var(a: float[], n: int) float {
    var s: float = 0.0
    var i: int = 0
    while (i < n) {
        s = s + a[i]
        i = i + 1
    }
    return s
}

pn count_lit(a: int[]) int {
    var c: int = 0
    var i: int = 0
    while (i < 4) {
        if (a[i] == null) { c = c + 1 }
        i = i + 1
    }
    return c
}

// nested counters: `j` starts at `i + 1`, both bounded by the literal. The
// result is boxed: a native float return drops a raised error (LR12-18).
pn pairs(a: float[]) any {
    var s: float = 0.0
    var i: int = 0
    while (i < 4) {
        var j: int = i + 1
        while (j < 4) {
            s = s + a[i] * a[j]
            j = j + 1
        }
        i = i + 1
    }
    return s
}

pn main() {
    var long: float[] = [1.0, 2.0, 3.0, 4.0, 5.0]
    print("lit_long=" ++ sum_lit(long) ++ "\n")
    print("var_long=" ++ sum_var(long, 5) ++ "\n")
    print("pairs_long=" ++ pairs(long) ++ "\n")
    var ints: int[] = [1, 2]
    print("count_short=" ++ count_lit(ints) ++ "\n")
    var short: float[] = [1.0, 2.0]
    print("pairs_short=" ++ pairs(short) ++ "\n")
}
