// Larceny Benchmark: primes
// Sieve of Eratosthenes — count primes up to 8000
// Adapted from Larceny/Gambit benchmark suite
// Expected: 1007 primes (well-known: pi(8000) = 1007)

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

pn benchmark() {
    // Run sieve 10 times for measurable time
    var result = 0
    var iter = 0
    while (iter < 10) {
        result = sieve(8000)
        iter = iter + 1
    }
    return result
}

pn main() {
    let result = benchmark()
    if (result == 1007) {
        print("primes: PASS\n")
    } else {
        print("primes: FAIL result=")
        print(result)
        print("\n")
    }
}
