// Tune26 G3 clock-resolution diagnostic: typed Sieve, 1,000 iterations.
pn sieve(var flags: bool[], sz: int) int {
    var prime_count: int = 0
    for i in 2 to sz {
        if (flags[i - 1]) {
            prime_count = prime_count + 1
            var k: int = i + i
            while (k <= sz) {
                flags[k - 1] = false
                k = k + i
            }
        }
    }
    return prime_count
}

pn benchmark() any {
    var checksum = 0
    for run in 1 to 1000 {
        var flags: bool[] = fill(5000, true)
        checksum = checksum + sieve(flags, 5000)
    }
    return checksum
}

pn main() {
    let __t0 = clock()
    let result = benchmark()
    let elapsed = (clock() - __t0) * 1000.0
    print("CHECKSUM: " ++ result ++ "\n")
    print("__TIMING__:" ++ elapsed ++ "\n")
}
