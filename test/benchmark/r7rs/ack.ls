// R7RS Benchmark: ack
// Ackermann function - ack(3, 8) = 2045
// Adapted from r7rs-benchmarks/src/ack.scm (scaled down for Lambda JIT)

pn ack(m, n) {
    if (m == 0) {
        return n + 1
    }
    if (n == 0) {
        return ack(m - 1, 1)
    }
    return ack(m - 1, ack(m, n - 1))
}

pn benchmark() {
    var result = ack(3, 8)
    return result
}

pn main() {
    let result = benchmark()
    if (result == 2045) {
        print("ack: PASS\n")
    } else {
        print("ack: FAIL result=")
        print(result)
        print("\n")
    }
}
