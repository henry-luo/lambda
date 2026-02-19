// R7RS Benchmark: cpstak (Typed version, adapted as tak2)
// Double-run Takeuchi function - tak(18, 12, 6) = 7, run twice
// Since Lambda JIT does not support closure-passing CPS patterns,
// we run the direct tak variant twice to approximate cpstak workload.
// Adapted from r7rs-benchmarks/src/cpstak.scm

pn tak(x: int, y: int, z: int) {
    if (y >= x) {
        return z
    }
    var a: int = tak(x - 1, y, z)
    var b: int = tak(y - 1, z, x)
    var c: int = tak(z - 1, x, y)
    return tak(a, b, c)
}

pn benchmark() {
    var result: int = tak(18, 12, 6)
    result = tak(18, 12, 6)
    return result
}

pn main() {
    let t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - t0) * 1000.0
    if (result == 7) {
        print("cpstak: PASS  ")
    } else {
        print("cpstak: FAIL result=")
        print(result)
        print(" ")
    }
    print(elapsed)
    print(" ms\n")
}
