// AWFY Benchmark: Sieve of Eratosthenes
// Expected result: 669
// Ported from JavaScript AWFY suite

pn make_array(n, val) {
    return fill(n, val)
}

pn sieve(flags, sz) {
    var prime_count = 0
    var i = 2
    while (i <= sz) {
        if (flags[i - 1]) {
            prime_count = prime_count + 1
            var k = i + i
            while (k <= sz) {
                flags[k - 1] = false
                k = k + i
            }
        }
        i = i + 1
    }
    return prime_count
}

pn benchmark() {
    var flags = make_array(5000, true)
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
