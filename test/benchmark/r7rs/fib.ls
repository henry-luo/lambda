// R7RS Benchmark: fib
// Naive recursive Fibonacci - fib(27) = 196418
// Adapted from r7rs-benchmarks/src/fib.scm (scaled down for Lambda JIT)

pn fib(n) {
    if (n < 2) {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

pn benchmark() {
    var result = fib(27)
    return result
}

pn main() {
    let result = benchmark()
    if (result == 196418) {
        print("fib: PASS\n")
    } else {
        print("fib: FAIL result=")
        print(result)
        print("\n")
    }
}
