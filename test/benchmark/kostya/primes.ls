// Kostya Benchmark: primes
// Count primes up to 1000000 using sieve of Eratosthenes
// Adapted from github.com/kostya/benchmarks
// Expected: pi(1000000) = 78498

pn sieve(limit) {
    var flags = fill(limit + 1, true)
    flags[0] = false
    flags[1] = false
    var i = 2
    while (i * i <= limit) {
        if (flags[i]) {
            var j = i * i
            while (j <= limit) {
                flags[j] = false
                j = j + i
            }
        }
        i = i + 1
    }
    var count = 0
    i = 2
    while (i <= limit) {
        if (flags[i]) {
            count = count + 1
        }
        i = i + 1
    }
    return count
}

pn main() {
    var __t0 = clock()
    let result = sieve(1000000)
    var __t1 = clock()
    if (result == 78498) {
        print("primes: PASS (" ++ string(result) ++ ")\n")
    } else {
        print("primes: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
