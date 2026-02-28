// Kostya Benchmark: collatz
// Longest Collatz (3n+1) sequence under a given limit
// Adapted from common benchmark problems
// Finds which starting number under 1000000 produces the longest sequence
// Expected: starting at 837799, length = 525

pn collatz_len(n) {
    var steps = 1
    var x = n
    while (x != 1) {
        if (x % 2 == 0) {
            x = shr(x, 1)
        } else {
            x = 3 * x + 1
        }
        steps = steps + 1
    }
    return steps
}

pn benchmark() {
    let limit = 1000000
    var max_len = 0
    var max_start = 0
    var i = 1
    while (i < limit) {
        var clen = collatz_len(i)
        if (clen > max_len) {
            max_len = clen
            max_start = i
        }
        i = i + 1
    }
    return max_start
}

pn main() {
    let result = benchmark()
    if (result == 837799) {
        print("collatz: PASS (start=" ++ string(result) ++ ")\n")
    } else {
        print("collatz: FAIL result=")
        print(result)
        print("\n")
    }
}
