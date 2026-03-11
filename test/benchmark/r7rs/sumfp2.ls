// R7RS Benchmark: sumfp (Typed version)
// Sum of floats from 0.0 to 100000.0, repeated 1 time
// Adapted from r7rs-benchmarks/src/sumfp.scm (scaled down for Lambda JIT)

pn run(n: float) {
    var s: float = 0.0
    while (n >= 0.0) {
        s = s + n
        n = n - 1.0
    }
    return s
}

pn benchmark() {
    var result = run(100000.0)
    return result
}

pn main() {
    let t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - t0) * 1000.0
    let expected = 5000050000.0
    let diff = abs(result - expected)
    if (diff < 1.0) {
        print("sumfp: PASS\n")
    } else {
        print("sumfp: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ string(elapsed) ++ "\n")
}
