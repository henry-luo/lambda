// Larceny Benchmark: array1
// Array creation, mutation, and summation
// Adapted from Larceny/Gambit benchmark suite
// Creates array of 10000 elements, fills with indices, sums all
// Expected: sum(0..9999) = 49995000

pn benchmark() {
    let size = 10000
    var arr = fill(size, 0)

    // Fill with index values
    var i = 0
    while (i < size) {
        arr[i] = i
        i = i + 1
    }

    // Sum all elements, repeated 100 times
    var total = 0
    var iter = 0
    while (iter < 100) {
        var s = 0
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
    let result = benchmark()
    if (result == 49995000) {
        print("array1: PASS\n")
    } else {
        print("array1: FAIL result=")
        print(result)
        print("\n")
    }
}
