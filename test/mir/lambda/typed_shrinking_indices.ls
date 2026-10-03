// D3.3.4/S7.1.3v2: a shrinking two-index loop shares its entry extent proof.
pn reverse_span(var values: int[], lower: int, upper: int) int[] {
    var lo: int = lower
    var hi: int = upper
    while (lo < hi) {
        var saved: int = values[lo]
        values[lo] = values[hi]
        values[hi] = saved
        lo = lo + 1
        hi = hi - 1
    }
    return values
}

pn reverse_growing(var values: int[], lower: int, upper: int) int[] {
    var lo: int = lower
    var hi: int = upper
    while (lo < hi) {
        push(values, 9)
        var saved: int = values[lo]
        values[lo] = values[hi]
        values[hi] = saved
        lo = lo + 1
        hi = hi - 1
    }
    return values
}

pn main() {
    var values: int[] = [1, 2, 3, 4, 5]
    let snapshot = values
    print(reverse_span(values, 0, 4)) print("\n")
    print(snapshot) print("\n")
    var short: int[] = [1, 2]
    print([reverse_span(short, 0, 4) is error,
        reverse_span(short, -1, 1) is error]) print("\n")
    print(short) print("\n")
    var growing: int[] = [1, 2, 3, 4]
    print(reverse_growing(growing, 0, 3)) print("\n")
}
