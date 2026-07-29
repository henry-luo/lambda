// Larceny Benchmark: quicksort (Typed version)
// Quicksort an array of pseudo-random integers
// Adapted from Larceny/Gambit benchmark suite
// Sorts elements using Lomuto partition, verifies sorted order
// Expected: PASS (all elements in non-decreasing order)

// Simple pseudo-random number generator that stays in safe int range
pn lcg_next(seed: int) int {
    return (seed * 1664525 + 1013904223) % 1000000
}

pn partition(arr: int[], lo: int, hi: int) int {
    var pivot: int = arr[hi]
    var i: int = lo
    var j: int = lo
    while (j < hi) {
        if (arr[j] <= pivot) {
            var tmp: int = arr[i]
            arr[i] = arr[j]
            arr[j] = tmp
            i = i + 1
        }
        j = j + 1
    }
    var tmp2: int = arr[i]
    arr[i] = arr[hi]
    arr[hi] = tmp2
    return i
}

pn quicksort(arr: int[], lo: int, hi: int) int {
    if (lo >= hi) {
        return 0
    }
    var p: int = partition(arr, lo, hi)
    var left_hi: int = p - 1
    var right_lo: int = p + 1
    quicksort(arr, lo, left_hi)
    quicksort(arr, right_lo, hi)
    return 0
}

pn is_sorted(arr: int[], n: int) int {
    var i: int = 1
    while (i < n) {
        if (arr[i] < arr[i - 1]) {
            return 0
        }
        i = i + 1
    }
    return 1
}

pn benchmark() int {
    let size: int = 5000
    // fill(n, int) already infers a packed ArrayNum; an int[] annotation on the
    // local would re-tag the var as ANY and lose the direct-index path
    var arr = fill(size, 0)

    // Fill with deterministic descending + modular values
    var seed: int = 42
    var i: int = 0
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
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 1) {
        print("quicksort: PASS\n")
    } else {
        print("quicksort: FAIL\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
