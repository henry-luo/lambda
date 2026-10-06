// D3.3.3v3/D5.3.3: equivalent full contracts share metadata only within an activation.
type Numbers = int[]

pn repeated(count: int, first: bool) {
    var early_len: int = -1
    if (first) {
        var early: Numbers = fill(count, 7)
        early_len = len(early)
    }
    var a: int[] = fill(count, 2)
    var b: Numbers = fill(count, 3)
    return [early_len, a, b]
}

pn looped(count: int) {
    var latest: int[] = []
    var i: int = 0
    while (i < count) {
        var a: Numbers = fill(i, 4)
        var b: int[] = fill(i, 5)
        latest = b
        i = i + 1
    }
    return latest
}

pn other_lanes(count: int) {
    var a: float[] = fill(count, 1.5)
    var b: bool[] = fill(count, true)
    return [a, b]
}

pn mismatched(count: int, value) int[] { return fill(count, value) }

pn main() {
    print([repeated(3, false), repeated(2, true), repeated(0, true)])
    print("\n")
    print([looped(0), looped(3), other_lanes(2)])
    print("\n")
    print([repeated(-1, false) is error, repeated(int(inf), false) is error,
        repeated(int(nan), true) is error, mismatched(2, 1.5) is error,
        mismatched(2, null) is error, mismatched(2, 9)])
    print("\n")
}
