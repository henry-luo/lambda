// Tune30 §10.5: a leaf inlined at its call site inherits the interval of each
// argument, so the counter bounds of the caller's loops reach the body's own
// arithmetic. `(i + j) * (i + j + 1)` then needs no saturation test and no
// slow-add call, which is what took spectralnorm from 3.5x to 2.25x.

pn tune30_cell(i: int, j: int) float {
    return 1.0 / float((i + j) * (i + j + 1) / 2 + i + 1)
}

pn tune30_mul_row(n: int, v: float[], var out: float[]) any {
    var i: int = 0
    while (i < n) {
        out[i] = tune30_cell(i, i) * v[i]
        i = i + 1
    }
}

pn main() {
    var v: float[] = [1.0, 1.0, 1.0, 1.0]
    var out: float[] = [0.0, 0.0, 0.0, 0.0]
    tune30_mul_row(4, v, out)
    print(out)
    print("\n")
}
