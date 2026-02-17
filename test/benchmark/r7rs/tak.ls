// R7RS Benchmark: tak
// Takeuchi function - tak(18, 12, 6) = 7
// Adapted from r7rs-benchmarks/src/tak.scm (scaled down for Lambda JIT)

pn tak(x, y, z) {
    if (y >= x) {
        return z
    }
    var a = tak(x - 1, y, z)
    var b = tak(y - 1, z, x)
    var c = tak(z - 1, x, y)
    return tak(a, b, c)
}

pn benchmark() {
    var result = tak(18, 12, 6)
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
