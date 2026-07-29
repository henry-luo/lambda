// Larceny Benchmark: divrec (Typed version)
// Recursive integer division via repeated subtraction
// Adapted from Larceny/Gambit benchmark suite (divrec)
// Same as diviter but uses recursion instead of loops
// Expected: result = 500000000

pn divrec_div_helper(x: int, y: int, q: int) int {
    if (x < y) {
        return q
    }
    return divrec_div_helper(x - y, y, q + 1)
}

pn divrec_div(x: int, y: int) int {
    return divrec_div_helper(x, y, 0)
}

pn divrec_mod(x: int, y: int) int {
    if (x < y) {
        return x
    }
    return divrec_mod(x - y, y)
}

pn benchmark() int {
    var result: int = 0
    var iter: int = 0
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
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
