// LR12-24 / TE-15, TE-18 case 7: a failed deferred check skips to the block
// that DECLARES the binding it was establishing. For an accumulator that block
// is the fn body, so the defect becomes the function's result and crosses a
// plain `int`/`float` return on the unenumerated system channel (S7.4.3).
// The native return lane cannot represent an error, so it travels beside the
// value on the error lane; before LR12-24 the JIT republished the error Item's
// raw bits through the value lane instead -- `inf` from int, `nan` from float.
pn int_acc(a: int[], i: int) int {
    var s: int = 1
    s = s + a[i]
    return s
}

pn float_acc(a: float[], i: int) float {
    var s: float = 1.0
    s = s + a[i]
    return s
}

// TE-18 case 1: the same skip at a DECLARATION boundary. The out-of-range read
// admits null before the binding exists, so the block -- here the fn body --
// yields the error rather than the declared `float`.
pn read_sum(xs: float[], ys: float[], index: int) float {
    let left: float = xs[index]
    let right: float = ys[index]
    return left + right
}

pn main() {
    var ints: int[] = [10, 20]
    var floats: float[] = [1.5, 2.5]

    // the happy path is untouched: a clean call still returns its native value
    print("int_ok=" ++ int_acc(ints, 1) ++ "\n")
    print("float_ok=" ++ float_acc(floats, 1) ++ "\n")

    // an out-of-range read admits null, which the declared contract rejects
    let bad_int = int_acc(ints, 5)
    print("int_defect=" ++ (bad_int is error) ++ " type=" ++ type(bad_int) ++ "\n")
    let bad_float = float_acc(floats, 5)
    print("float_defect=" ++ (bad_float is error) ++ " type=" ++ type(bad_float) ++ "\n")

    print("decl_ok=" ++ read_sum(floats, floats, 1) ++ "\n")
    let bad_decl = read_sum(floats, floats, 9)
    print("decl_defect=" ++ (bad_decl is error) ++ " type=" ++ type(bad_decl) ++ "\n")

    // containment, not abort: the caller's own block keeps running, and a
    // later clean call still takes the native lane
    print("still_native=" ++ int_acc(ints, 0) ++ "\n")
}
