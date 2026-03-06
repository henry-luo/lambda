// AWFY Benchmark: Sieve of Eratosthenes (Typed version)
// Expected result: 669

pn make_array(n: int, val) {
    return fill(n, val)
}

pn sieve(flags, sz: int) {
    var prime_count: int = 0
    var i: int = 2
    while (i <= sz) {
        if (flags[i - 1]) {
            prime_count = prime_count + 1
            var k: int = i + i
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
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 669) {
        print("Sieve: PASS\n")
    } else {
        print("Sieve: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
