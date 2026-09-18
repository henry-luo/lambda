// T30-4: the finite-loop plan admits affine conditions, affine and scaling
// updates. Its entry guard must send out-of-range counters, bounds and steps
// to the generic arm, where saturation (S4.1.2) and out-of-range reads
// (S7.1.3v2) behave exactly as the interpreter does.
pn affine_sum(data: int[], m: int) int {
    var total: int = 0
    var nc1: int = 0
    while (nc1 * 4 <= m) {
        var nc2: int = nc1
        while (nc1 + nc2 * 3 <= m) {
            var remain: int = m - nc1 - nc2
            var nc3: int = nc2
            while (nc3 * 2 <= remain) {
                total = total + data[nc3] + nc1
                nc3 = nc3 + 1
            }
            nc2 = nc2 + 1
        }
        nc1 = nc1 + 1
    }
    return total
}

pn strided(data: int[], n: int, stride: int) int {
    var acc: int = 0
    var i: int = 0
    while (i < n) {
        var j: int = i + stride
        acc = acc + data[i] + j
        i = i + stride + stride
    }
    return acc
}

pn doubling(n: int) int {
    var steps: int = 0
    var m: int = 1
    while (m < n) {
        steps = steps + m
        m = m * 2
    }
    return steps
}

pn main() {
    var data: int[] = [1, 2, 3, 4, 5, 6, 7, 8]
    print("affine=" ++ affine_sum(data, 6) ++ " " ++ affine_sum(data, 0) ++ " " ++
        affine_sum(data, 0 - 3) ++ "\n")
    print("strided=" ++ strided(data, 8, 1) ++ " " ++ strided(data, 8, 3) ++ "\n")
    // a step past STEP_MAX leaves the guarded arm; so does a bound past it
    print("wide=" ++ strided(data, 8, 100000) ++ " " ++ strided(data, 1, 999999) ++ "\n")
    print("doubling=" ++ doubling(100) ++ " " ++ doubling(1) ++ " " ++ doubling(0 - 5) ++ "\n")
    print("doubling2=" ++ doubling(100000000) ++ "\n")
}
