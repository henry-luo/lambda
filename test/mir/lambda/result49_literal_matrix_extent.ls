// fixed-stride matrix reads and a private destination use one guarded extent.
pn mul2(a: float[], b: float[]) float[] {
    var out: float[] = fill(4, 0.0)
    var i: int = 0
    while (i < 2) {
        var j: int = 0
        while (j < 2) {
            out[i * 2 + j] = a[i * 2 + 0] * b[0 * 2 + j] +
                a[i * 2 + 1] * b[1 * 2 + j]
            j = j + 1
        }
        i = i + 1
    }
    return out
}

pn inclusive_overrun(a: float[]) float {
    var sum: float = 0.0
    var i: int = 0
    while (i < 2) {
        var j: int = 0
        while (j <= 2) {
            sum = sum + a[i * 2 + j]
            j = j + 1
        }
        i = i + 1
    }
    return sum
}

pn main() {
    let b: float[] = [5.0, 6.0, 7.0, 8.0]
    print(mul2([1.0, 2.0, 3.0, 4.0], b)); print("\n")
    var rejected = false
    mul2([1.0], b) ^ { rejected = true }
    print(rejected); print("\n")
    var inclusive_rejected = false
    inclusive_overrun([1.0, 2.0, 3.0, 4.0]) ^ { inclusive_rejected = true }
    print(inclusive_rejected); print("\n")
}
