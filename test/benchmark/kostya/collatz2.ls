// Kostya Benchmark: collatz (Typed version)
// Longest Collatz (3n+1) sequence under a given limit
// Adapted from common benchmark problems
// Finds which starting number under 1000000 produces the longest sequence
// Expected: starting at 837799, length = 525

pn collatz_len(n: int) int {
    var steps: int = 1
    while (n != 1) {
        if (n % 2 == 0) {
            n = shr(n, 1)
        } else {
            n = 3 * n + 1
        }
        steps = steps + 1
    }
    return steps
}

pn benchmark() int {
    let limit: int = 1000000
    var max_len: int = 0
    var max_start: int = 0
    var i: int = 1
    while (i < limit) {
        var clen: int = collatz_len(i)
        if (clen > max_len) {
            max_len = clen
            max_start = i
        }
        i = i + 1
    }
    return max_start
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 837799) {
        print("collatz: PASS (start=" ++ result ++ ")\n")
    } else {
        print("collatz: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
