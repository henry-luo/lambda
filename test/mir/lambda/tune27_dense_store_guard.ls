// Result44 fix (Tune27 §12): T26-4's unique-owner guard for the `var` store
// root `c` is conjoined into a SEPARATE store guard; the reads of `a` and `b`
// keep the pure extent proof even when `c`'s ownership word fails at loop
// entry (typed matmul ran its whole nest on the checked arm, 3x).

pn matmul(a: float[], b: float[], var c: float[], n: int) any {
    var i: int = 0
    while (i < n) {
        var j: int = 0
        while (j < n) {
            var sum: float = 0.0
            var k: int = 0
            while (k < n) {
                sum = sum + a[i * n + k] * b[k * n + j]
                k = k + 1
            }
            c[i * n + j] = sum
            j = j + 1
        }
        i = i + 1
    }
}

pn main() {
    var a: float[] = fill(4, 1.5)
    var b: float[] = fill(4, 2.0)
    var c: float[] = fill(4, 0.0)
    matmul(a, b, c, 2)
    print(c)
    print("\n")
}
