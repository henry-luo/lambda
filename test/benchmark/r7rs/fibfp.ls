// R7RS Benchmark: fibfp
// Fibonacci using floating-point arithmetic - fibfp(27.0) = 196418.0
// Adapted from r7rs-benchmarks/src/fibfp.scm (scaled down for Lambda JIT)

pn fibfp(n) {
    if (n < 2.0) {
        return n
    }
    return fibfp(n - 1.0) + fibfp(n - 2.0)
}

pn benchmark() {
    var result = fibfp(27.0)
    return result
}

pn main() {
    let t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - t0) * 1000.0
    if (result == 196418.0) {
        print("fibfp: PASS\n")
    } else {
        print("fibfp: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ elapsed ++ "\n")
}
