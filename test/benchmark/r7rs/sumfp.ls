// R7RS Benchmark: sumfp
// Sum of floats from 0.0 to 100000.0, repeated 1 time
// Adapted from r7rs-benchmarks/src/sumfp.scm (scaled down for Lambda JIT)

pn run(n) {
    var i = n
    // Use Item-typed zero to avoid transpiler float reassignment bug
    var s = n - n
    while (i >= 0.0) {
        s = s + i
        i = i - 1.0
    }
    return s
}

pn benchmark() {
    var result = run(100000.0)
    return result
}

pn main() {
    let result = benchmark()
    let expected = 5000050000.0
    let diff = abs(result - expected)
    if (diff < 1.0) {
        print("sumfp: PASS\n")
    } else {
        print("sumfp: FAIL result=")
        print(result)
        print("\n")
    }
}
