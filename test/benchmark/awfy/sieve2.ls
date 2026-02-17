// AWFY Benchmark: Sieve of Eratosthenes (Typed version)
// Expected result: 669

pn make_array(n: int, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz: int = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var extra = [val]
        var esz: int = 1
        var need: int = n - sz
        while (esz * 2 <= need) {
            extra = extra ++ extra
            esz = esz * 2
        }
        if (esz < need) {
            var rest = [val]
            var rsz: int = 1
            while (rsz < need - esz) {
                rest = rest ++ [val]
                rsz = rsz + 1
            }
            extra = extra ++ rest
        }
        arr = arr ++ extra
    }
    return arr
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
    let result = benchmark()
    if (result == 669) {
        print("Sieve: PASS\n")
    } else {
        print("Sieve: FAIL result=")
        print(result)
        print("\n")
    }
}
