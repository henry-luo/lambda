// T19-C: an ORDERED comparison between two int lanes must lower to a native
// integer compare even when one side is a declared `int[]` read, whose public
// type is `int?` (D2.5.3). Before this the pair was widened through
// emit_int_lane_to_double -- a band test, an i2d and a cold call PER OPERAND,
// including for the literal 0, whose value the compiler already knows.
//
// The sentinel arm keeps the float lowering: that is what makes a null compare
// false (S7.10.3) and a saturated bound order as +/-inf (S4.1.2). So this body
// must contain BOTH -- the native integer fast arm AND the single double
// compare behind the band guard.

pn count_positive() int {
    var counts: int[] = fill(4, 0)
    counts[0] = 3
    counts[1] = 0 - 2
    counts[2] = 7
    var i: int = 0
    var positive: int = 0
    while (i < 4) {
        if (counts[i] > 0) {
            positive = positive + 1
        }
        i = i + 1
    }
    return positive
}

pn main() {
    print("positive=" ++ count_positive() ++ "\n")
}
