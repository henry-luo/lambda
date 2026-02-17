// R7RS Benchmark: tak (Typed version)
// Takeuchi function - tak(18, 12, 6) = 7
// Adapted from r7rs-benchmarks/src/tak.scm (scaled down for Lambda JIT)

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
    return result
}

pn main() {
    let result = benchmark()
    if (result == 7) {
        print("tak: PASS\n")
    } else {
        print("tak: FAIL result=")
        print(result)
        print("\n")
    }
}
