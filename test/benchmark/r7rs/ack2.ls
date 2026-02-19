// R7RS Benchmark: ack (Typed version)
// Ackermann function - ack(3, 8) = 2045
// Adapted from r7rs-benchmarks/src/ack.scm (scaled down for Lambda JIT)

pn ack(m: int, n: int) {
    if (m == 0) {
        return n + 1
    }
    if (n == 0) {
        return ack(m - 1, 1)
    }
    return ack(m - 1, ack(m, n - 1))
}

pn benchmark() {
    var result: int = ack(3, 8)
    return result
}

pn main() {
    let t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - t0) * 1000.0
    if (result == 2045) {
        print("ack: PASS  ")
    } else {
        print("ack: FAIL result=")
        print(result)
        print(" ")
    }
    print(elapsed)
    print(" ms\n")
}
