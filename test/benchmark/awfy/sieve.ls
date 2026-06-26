// AWFY Benchmark: Sieve of Eratosthenes
// Expected result: 669
// Ported from JavaScript AWFY suite

pn sieve(flags, sz) {
    var prime_count = 0
    for i in 2 to sz {
        if (flags[i - 1]) {
            prime_count = prime_count + 1
            var k = i + i
            while (k <= sz) {
                flags[k - 1] = false
                k = k + i
            }
        }
    }
    return prime_count
}

pn benchmark() {
    var flags = fill(5000, true)
    return sieve(flags, 5000)
}

pn main() {
    let result = benchmark()
    if (result == 669) {
        print("Sieve: PASS\n")
    } else {
        print("Sieve: FAIL result=")
        print(result)
        print("\n")
    }
}
