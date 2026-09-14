// Tune27 T27-9 / S7.1.3v2 / D3.3.3v3: a literal loop bound (`while (i < 4)`)
// is an extent; a nested counter declared inside the body joins the proof; a
// finite loop's counter fact starting at zero proves it non-negative. Every
// read below is a raw lane load on the dense arm and the body has no
// contract boundary.

pn pair_sums(bx: float[], by: float[]) float {
    var total: float = 0.0
    var i: int = 0
    while (i < 4) {
        var j: int = i + 1
        while (j < 4) {
            var dx: float = bx[i] - bx[j]
            var dy: float = by[i] - by[j]
            total = total + dx * dx + dy * dy
            j = j + 1
        }
        i = i + 1
    }
    return total
}

pn main() {
    let bx: float[] = [0.0, 1.0, 2.0, 4.0]
    let by: float[] = [0.0, 0.5, 1.5, 3.5]
    print(pair_sums(bx, by))
    print("\n")
    // a short array takes the checked arm and reads null out of range
    print(pair_sums([1.0], [1.0]) == null)
    print("\n")
}
