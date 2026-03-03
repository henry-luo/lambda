// Larceny Benchmark: quicksort
// Quicksort an array of pseudo-random integers
// Adapted from Larceny/Gambit benchmark suite
// Sorts elements using Lomuto partition, verifies sorted order
// Expected: PASS (all elements in non-decreasing order)

// Simple pseudo-random number generator that stays in safe int range
pn lcg_next(seed) {
    return (seed * 1664525 + 1013904223) % 1000000
}

pn partition(arr, lo, hi) {
    var pivot = arr[hi]
    var i = lo
    var j = lo
    while (j < hi) {
        if (arr[j] <= pivot) {
            var tmp = arr[i]
            arr[i] = arr[j]
            arr[j] = tmp
            i = i + 1
        }
        j = j + 1
    }
    var tmp2 = arr[i]
    arr[i] = arr[hi]
    arr[hi] = tmp2
    return i
}

pn quicksort(arr, lo, hi) {
    if (lo >= hi) {
        return 0
    }
    var p = partition(arr, lo, hi)
    var left_hi = p - 1
    var right_lo = p + 1
    quicksort(arr, lo, left_hi)
    quicksort(arr, right_lo, hi)
    return 0
}

pn is_sorted(arr, n) {
    var i = 1
    while (i < n) {
        if (arr[i] < arr[i - 1]) {
            return 0
        }
        i = i + 1
    }
    return 1
}

pn benchmark() {
    let size = 5000
    var arr = fill(size, 0)

    // Fill with deterministic descending + modular values
    var seed = 42
    var i = 0
    while (i < size) {
        seed = lcg_next(seed)
        arr[i] = seed
        i = i + 1
    }

    // Sort
    quicksort(arr, 0, size - 1)

    return is_sorted(arr, size)
}

pn main() {
    let result = benchmark()
    if (result == 1) {
        print("quicksort: PASS\n")
    } else {
        print("quicksort: FAIL\n")
    }
}
