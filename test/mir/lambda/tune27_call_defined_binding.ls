// Tune27 T27-8 (§10.11, D8.3.2): an unannotated local defined only by calls
// to a local function with an explicit, non-raising `float[]` return crosses
// that contract nowhere -- not at each `rotate(m, ...)` argument, not at the
// declaration.

pn rotate(m: float[], phi: float) float[] {
    return [m[0] * phi, m[1] * phi, m[2] + phi]
}

pn make() float[] {
    return [1.0, 2.0, 3.0]
}

pn chain(n: int) float {
    var m = make()
    var k: int = 0
    while (k < n) {
        m = rotate(m, 2.0)
        m = rotate(m, 0.5)
        k = k + 1
    }
    return m[0] + m[1] + m[2]
}

pn main() {
    print(chain(3))
    print("\n")
}
