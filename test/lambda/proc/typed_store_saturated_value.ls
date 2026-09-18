// Tune30 T30-2: a value read from a certified lane of the same element type
// needs no band test on a typed store. The fact rests on two behaviours this
// fixture pins, on both tiers: `int[]` holds a saturated `inf` (S4.1.2), and
// the checked setter stores that lane value unchanged. Only an absent value
// must still be rejected (S7.1.3v2), which is what the remaining check is for.
pn copy_cell(var dst: int[], src: int[], i: int, j: int) any {
    dst[i] = src[j]
}

pn copy_float(var dst: float[], src: float[], i: int, j: int) any {
    dst[i] = src[j]
}

pn copy_loop(var dst: int[], src: int[], n: int) any {
    var i: int = 0
    while (i < n) {
        dst[i] = src[i]
        i = i + 1
    }
}

pn main() {
    var big: int = 9007199254740991
    var a: int[] = [1, 2, 3]
    a[0] = big + 1
    a[2] = 0 - big - 1
    print("stored=" ++ a ++ "\n")
    // the saturated lane survives a copy through a typed store
    var b: int[] = [0, 0, 0]
    copy_cell(b, a, 1, 0)
    copy_cell(b, a, 2, 2)
    print("copied=" ++ b ++ "\n")
    // and through the loop form, where the store takes the proven arm
    var c: int[] = [0, 0, 0]
    copy_loop(c, a, 3)
    print("loop=" ++ c ++ "\n")
    // an out-of-range read is null, which the typed store must still reject
    copy_cell(b, a, 0, 9)
    print("after_oob=" ++ b ++ "\n")
    // floats carry their own infinities the same way
    var f: float[] = [1.0, 2.0]
    f[0] = 1.0 / 0.0
    var g: float[] = [0.0, 0.0]
    copy_float(g, f, 1, 0)
    print("float=" ++ f ++ " " ++ g ++ "\n")
    print("done\n")
}
