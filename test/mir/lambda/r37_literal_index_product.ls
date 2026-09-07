// Result37 (cube3d 1.20x): `i * 4 + j` in while loops has no counted-loop proof
pn mul4(m1: float[], m2: float[], m: float[]) float {
    var i = 0
    while (i < 4) {
        var j = 0
        while (j < 4) {
            m[i * 4 + j] = m1[i * 4 + 0] * m2[0 * 4 + j] + m1[i * 4 + 1] * m2[1 * 4 + j] +
                           m1[i * 4 + 2] * m2[2 * 4 + j] + m1[i * 4 + 3] * m2[3 * 4 + j]
            j = j + 1
        }
        i = i + 1
    }
    return m[0] + m[15]
}
pn main() {
    var a: float[] = fill(16, 1.0)
    var b: float[] = fill(16, 2.0)
    var c: float[] = fill(16, 0.0)
    print(mul4(a, b, c), "\n")
}
