// Kostya Benchmark: matmul
// Matrix multiplication — multiply two NxN matrices
// Adapted from github.com/kostya/benchmarks
// N=200, multiplied once, result = sum of all elements in result matrix
// Expected: deterministic result from LCG init

let N = 200

pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = [val]
        var esz = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn next_rand(seed) {
    return (seed * 1664525 + 1013904223) % 1000000
}

pn matmul(a, b, c, n) {
    var i = 0
    while (i < n) {
        var j = 0
        while (j < n) {
            var sum = 0.0
            var k = 0
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
    let size = N * N
    var a = make_array(size, 0.0)
    var b = make_array(size, 0.0)
    var c = make_array(size, 0.0)

    // Initialize with pseudo-random values in [-1.0, 1.0)
    var seed = 42
    var i = 0
    while (i < size) {
        seed = next_rand(seed)
        a[i] = float(seed % 2000) / 1000.0 - 1.0
        seed = next_rand(seed)
        b[i] = float(seed % 2000) / 1000.0 - 1.0
        i = i + 1
    }

    // Multiply
    matmul(a, b, c, N)

    // Sum result matrix
    var total = 0.0
    i = 0
    while (i < size) {
        total = total + c[i]
        i = i + 1
    }

    // Print truncated sum for verification
    let int_total = int(floor(total))
    print("matmul: sum=" ++ string(int_total) ++ "\n")
    print("matmul: DONE\n")
}
