// JetStream Benchmark: bigdenary
// Big decimal arbitrary-precision arithmetic
// Original: BigDenary library (U-Zyn Chua) - uses JS BigInt
// Tests decimal arithmetic operations: add, subtract, multiply, divide, compare, negate
// Lambda uses built-in unlimited-precision decimal type (N suffix)
// Note: decimal values cannot be passed as function arguments (issue #17),
//       so we use global constants directly inside helper functions.

// The two test numbers from the original benchmark
let BD1 = 8965168485622506189945604.1235068121348084163185216N
let BD2 = 2480986213549488579706531.6546845013548451265890628N

pn test_plus() {
    var first = BD1 + BD2
    var last = first
    var i: int = 0
    while (i < 10000) {
        last = BD1 + BD2
        i = i + 1
    }
    return first == last
}

pn test_minus() {
    var first = BD1 - BD2
    var last = first
    var i: int = 0
    while (i < 10000) {
        last = BD1 - BD2
        i = i + 1
    }
    return first == last
}

pn test_negate() {
    var first = -BD1
    var last = first
    var i: int = 0
    while (i < 10000) {
        last = -BD1
        i = i + 1
    }
    return first == last
}

pn test_compare() {
    var first = BD1 > BD2
    var last = first
    var i: int = 0
    while (i < 10000) {
        last = BD1 > BD2
        i = i + 1
    }
    return first == last
}

pn test_multiply() {
    var first = BD1 * BD2
    var last = first
    var i: int = 0
    while (i < 10000) {
        last = BD1 * BD2
        i = i + 1
    }
    return first == last
}

pn test_divide() {
    var first = BD1 / BD2
    var last = first
    var i: int = 0
    while (i < 10000) {
        last = BD1 / BD2
        i = i + 1
    }
    return first == last
}

pn run() {
    if (test_plus() == false) { return false }
    if (test_minus() == false) { return false }
    if (test_negate() == false) { return false }
    if (test_compare() == false) { return false }
    if (test_multiply() == false) { return false }
    if (test_divide() == false) { return false }
    return true
}

pn main() {
    var __t0 = clock()
    // JetStream runs 1 iteration (each iteration does 6 ops × 10001 runs)
    var pass = true
    var iter: int = 0
    while (iter < 1) {
        if (run() == false) {
            pass = false
        }
        iter = iter + 1
    }
    var __t1 = clock()
    if (pass) {
        print("bigdenary: PASS\n")
    } else {
        print("bigdenary: FAIL\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
