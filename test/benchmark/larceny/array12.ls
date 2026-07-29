// Larceny Benchmark: array1 (Typed version)
// Array creation, mutation, and summation
// Adapted from Larceny/Gambit benchmark suite
// Creates array of 10000 elements, fills with indices, sums all
// Expected: sum(0..9999) = 49995000

pn benchmark() int {
    let size: int = 10000
    // no int[] annotation on the local: fill(n, int) is already inferred as a
    // packed ArrayNum, and the bracket annotation would re-tag the var as ANY
    var arr = fill(size, 0)

    // Fill with index values
    var i: int = 0
    while (i < size) {
        arr[i] = i
        i = i + 1
    }

    // Sum all elements, repeated 100 times
    var total: int = 0
    var iter: int = 0
    while (iter < 100) {
        var s: int = 0
        i = 0
        while (i < size) {
            s = s + arr[i]
            i = i + 1
        }
        total = s
        iter = iter + 1
    }
    return total
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 49995000) {
        print("array1: PASS\n")
    } else {
        print("array1: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
