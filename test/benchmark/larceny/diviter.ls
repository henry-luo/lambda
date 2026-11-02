// Larceny Benchmark: diviter
// Iterative integer division via repeated subtraction
// Adapted from Larceny/Gambit benchmark suite (diviter)
// Performs div/mod by repeated subtraction, 1000 iterations
// Expected: result = 500000

fn diviter_div(x, y) {
    var work_x = x

    var q = 0
    while (work_x >= y) {
        work_x = work_x - y
        q = q + 1
    }
    return q
}

fn diviter_mod(x, y) {
    var work_x = x

    while (work_x >= y) {
        work_x = work_x - y
    }
    return work_x
}

fn benchmark() {
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
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 500000000) {
        print("diviter: PASS\n")
    } else {
        print("diviter: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
