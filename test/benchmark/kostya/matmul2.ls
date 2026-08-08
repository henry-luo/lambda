// Kostya Benchmark: matmul (Typed version)
// Matrix multiplication — multiply two NxN matrices
// Adapted from github.com/kostya/benchmarks
// N=200, multiplied once, result = sum of all elements in result matrix
// Expected: deterministic result from LCG init

let N = 200

pn next_rand(seed: int) int {
    return int((seed * 1664525 + 1013904223) % 1000000)
}

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
    var __t0 = clock()
    let size: int = N * N
    // The annotation binds the public contract; fill retains packed float
    // storage until a checked write requires a representation change.
    var a: float[] = fill(size, 0.0)
    var b: float[] = fill(size, 0.0)
    var c: float[] = fill(size, 0.0)

    // Initialize with pseudo-random values in [-1.0, 1.0)
    var seed: int = 42
    var i: int = 0
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
    var total: float = 0.0
    i = 0
    while (i < size) {
        total = total + c[i]
        i = i + 1
    }
    var __t1 = clock()

    // Print truncated sum for verification
    let int_total: int = int(floor(total))
    print("matmul: sum=" ++ int_total ++ "\n")
    print("matmul: DONE\n")
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
