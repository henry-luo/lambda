// Larceny Benchmark: divrec
// Recursive integer division via repeated subtraction
// Adapted from Larceny/Gambit benchmark suite (divrec)
// Same as diviter but uses recursion instead of loops
// Expected: result = 500000000

pn divrec_div_helper(x, y, q) {
    if (x < y) {
        return q
    }
    return divrec_div_helper(x - y, y, q + 1)
}

pn divrec_div(x, y) {
    return divrec_div_helper(x, y, 0)
}

pn divrec_mod(x, y) {
    if (x < y) {
        return x
    }
    return divrec_mod(x - y, y)
}

pn benchmark() {
    var result = 0
    var iter = 0
    while (iter < 1000) {
        result = result + divrec_div(1000, 2)
        result = result - divrec_mod(1000, 2)
        iter = iter + 1
    }
    return result
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 500000) {
        print("divrec: PASS\n")
    } else {
        print("divrec: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
