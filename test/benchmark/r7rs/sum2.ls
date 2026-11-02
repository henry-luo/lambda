// R7RS Benchmark: sum (Typed version)
// Sum of integers from 0 to 10000, repeated 100 times
// Adapted from r7rs-benchmarks/src/sum.scm (scaled down for Lambda JIT)

fn run(n: int) int {
    var work_n: int = n

    var s: int = 0
    while (work_n >= 0) {
        s = s + work_n
        work_n = work_n - 1
    }
    return s
}

fn benchmark() int {
    var result: int = 0
    var iter: int = 0
    while (iter < 100) {
        result = run(10000)
        iter = iter + 1
    }
    return result
}

pn main() {
    let t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - t0) * 1000.0
    if (result == 50005000) {
        print("sum: PASS\n")
    } else {
        print("sum: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ elapsed ++ "\n")
}
