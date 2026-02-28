// Larceny Benchmark: diviter
// Iterative integer division via repeated subtraction
// Adapted from Larceny/Gambit benchmark suite (diviter)
// Performs div/mod by repeated subtraction, 1000 iterations
// Expected: result = 500000

pn diviter_div(x, y) {
    var q = 0
    var r = x
    while (r >= y) {
        r = r - y
        q = q + 1
    }
    return q
}

pn diviter_mod(x, y) {
    var r = x
    while (r >= y) {
        r = r - y
    }
    return r
}

pn benchmark() {
    var result = 0
    var iter = 0
    while (iter < 1000) {
        result = result + diviter_div(1000000, 2)
        result = result - diviter_mod(1000000, 2)
        iter = iter + 1
    }
    return result
}

pn main() {
    let result = benchmark()
    if (result == 500000000) {
        print("diviter: PASS\n")
    } else {
        print("diviter: FAIL result=")
        print(result)
        print("\n")
    }
}
