// Kostya Benchmark: primes
// Count primes up to 1000000 using sieve of Eratosthenes
// Adapted from github.com/kostya/benchmarks
// Expected: pi(1000000) = 78498

pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = [val]
        var esz = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn sieve(limit) {
    var flags = make_array(limit + 1, true)
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
    let result = sieve(1000000)
    if (result == 78498) {
        print("primes: PASS (" ++ string(result) ++ ")\n")
    } else {
        print("primes: FAIL result=")
        print(result)
        print("\n")
    }
}
