// Guards for the MIR inline typed-element fast paths (Tune4 M1).
//
// A declared `float[]` / `int[]` parameter only fixes the element
// representation at entry: a heterogeneous store makes fn_array_set convert the
// ArrayNum to a generic Array in place. Every inline load/store on a declared
// element type therefore re-checks the runtime representation and falls back,
// and a body that performs such a store keeps its reads boxed entirely.
//
// The index cases pin the native-int subscript path: `r * w + c` is INT-typed
// but boxes as a flexible int, and must still reach the inline element access.

pn sum_floats(a: float[], n: int) {
    var s = 0.0
    var i = 0
    while (i < n) {
        s = s + a[i]
        i = i + 1
    }
    return s
}

pn sum_ints(a: int[], n: int) {
    var s = 0
    var i = 0
    while (i < n) {
        s = s + a[i]
        i = i + 1
    }
    return s
}

pn row_sum(a: float[], rows: int, w: int, c: int) {
    var s = 0.0
    var r = 0
    while (r < rows) {
        s = s + a[r * w + c]
        r = r + 1
    }
    return s
}

// representation-preserving store — stays an ArrayNum
pn fill_diagonal(a: float[], rows: int, w: int) {
    var r = 0
    while (r < rows) {
        a[r * w + r] = float(r) + 0.5
        r = r + 1
    }
    return string(a)
}

// representation-CHANGING store — converts to a generic Array in place, so the
// reads after it must not decode Items as raw doubles
pn convert_inside(a: float[]) {
    var before = string(a[0])
    a[1] = "x"
    return before ++ ";" ++ string(a[1]) ++ ";" ++ string(a[2])
}

pn convert_inside_int(a: int[]) {
    var before = string(a[0])
    a[1] = 3.5
    return before ++ ";" ++ string(a[1]) ++ ";" ++ string(a[2])
}

pn main() {
    print(string(sum_floats(fill(4, 2.5), 4)) ++ "\n")
    print(string(sum_ints(fill(4, 7), 4)) ++ "\n")
    print(string(row_sum(fill(6, 1.25), 3, 2, 1)) ++ "\n")
    print(fill_diagonal(fill(6, 1.25), 3, 2) ++ "\n")
    print(convert_inside(fill(4, 2.5)) ++ "\n")
    print(convert_inside_int(fill(4, 7)) ++ "\n")
    // a generic float array coerces into a float[] parameter
    print(string(sum_floats([1.0, 2.0, 3.0, 4.0], 4)) ++ "\n")
}
