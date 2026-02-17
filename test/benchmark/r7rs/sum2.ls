// R7RS Benchmark: sum (Typed version)
// Sum of integers from 0 to 10000, repeated 100 times
// Adapted from r7rs-benchmarks/src/sum.scm (scaled down for Lambda JIT)

pn run(n: int) {
    var i: int = n
    var s: int = 0
    while (i >= 0) {
        s = s + i
        i = i - 1
    }
    return s
}

pn benchmark() {
    var result: int = 0
    var iter: int = 0
    while (iter < 100) {
        result = run(10000)
        iter = iter + 1
    }
    return result
}

pn main() {
    let result = benchmark()
    if (result == 50005000) {
        print("sum: PASS\n")
    } else {
        print("sum: FAIL result=")
        print(result)
        print("\n")
    }
}
