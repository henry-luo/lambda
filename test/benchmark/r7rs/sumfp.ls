// R7RS Benchmark: sumfp
// Sum of floats from 0.0 to 100000.0, repeated 1 time
// Adapted from r7rs-benchmarks/src/sumfp.scm (scaled down for Lambda JIT)

fn run(n) {
    var work_n = n

    var s = 0.0
    while (work_n >= 0.0) {
        s = s + work_n
        work_n = work_n - 1.0
    }
    return s
}

fn benchmark() {
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
    print("__TIMING__:" ++ elapsed ++ "\n")
}
