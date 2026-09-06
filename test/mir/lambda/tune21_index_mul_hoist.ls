// T21-1b: a counted-loop index product lowers to a pure `mul` whose validity is
// decided branch-free in the double lane; no flag-carrying `mulo`/`bo` pair may
// split the inner loop, and the poison select is branch-free too.
pn dot_rows(a: float[], b: float[], n: int) float {
    var acc: float = 0.0
    var i: int = 0
    while (i < n) {
        var k: int = 0
        while (k < n) {
            acc = acc + a[i * n + k] * b[k * n + i]
            k = k + 1
        }
        i = i + 1
    }
    return acc
}
pn main() {
    let n = 4
    var a: float[] = fill(n * n, 1.5)
    var b: float[] = fill(n * n, 2.0)
    print(dot_rows(a, b, n)); print("\n")
}
