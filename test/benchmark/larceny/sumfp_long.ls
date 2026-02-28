// Larceny Benchmark: sumfp_long
// Extended floating-point summation benchmark
// Adapted from Larceny/Gambit benchmark suite
// Sum 1/i^2 for i=1 to 500000, repeated 5 times (pi^2/6 approximation)
// Expected: approximately 1.6449330668... (pi^2/6)

pn partial_sum(n) {
    var s = 0.0
    var i = 1
    while (i <= n) {
        var fi = float(i)
        s = s + 1.0 / (fi * fi)
        i = i + 1
    }
    return s
}

pn benchmark() {
    var result = 0.0
    var iter = 0
    while (iter < 5) {
        result = partial_sum(500000)
        iter = iter + 1
    }
    return result
}

pn main() {
    let result = benchmark()
    // pi^2/6 ≈ 1.6449340668...
    // With 500000 terms, result ≈ 1.6449310668...
    let int_part = int(floor(result * 10000.0))
    if (int_part == 16449) {
        print("sumfp_long: PASS\n")
    } else {
        print("sumfp_long: FAIL result=")
        print(result)
        print("\n")
    }
}
