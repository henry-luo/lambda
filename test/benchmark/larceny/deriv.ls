// Larceny Benchmark: deriv
// Symbolic differentiation of expression trees
// Adapted from Larceny/Gambit benchmark suite
// Differentiates 3*x^3 + 2*x^2 + x + 5, repeats 5000 times, counts nodes
// Expected: 45 nodes in derivative

// Expression tags: 0=const, 1=var(x), 2=add, 3=mul

pn deriv(e) {
    if (e.t == 0) {
        return {t: 0, v: 0}
    }
    if (e.t == 1) {
        return {t: 0, v: 1}
    }
    if (e.t == 2) {
        var dl = deriv(e.l)
        var dr = deriv(e.r)
        return {t: 2, l: dl, r: dr}
    }
    // t == 3: product rule  d(a*b) = a*db + da*b
    var dl = deriv(e.l)
    var dr = deriv(e.r)
    var p1 = {t: 3, l: e.l, r: dr}
    var p2 = {t: 3, l: dl, r: e.r}
    return {t: 2, l: p1, r: p2}
}

pn count_nodes(e) {
    if (e.t == 0 or e.t == 1) {
        return 1
    }
    return 1 + count_nodes(e.l) + count_nodes(e.r)
}

pn make_expr() {
    // 3*x*x*x + 2*x*x + x + 5
    var c3 = {t: 0, v: 3}
    var c2 = {t: 0, v: 2}
    var c5 = {t: 0, v: 5}
    var x1 = {t: 1, v: 0}
    var x2 = {t: 1, v: 0}
    var x3 = {t: 1, v: 0}
    var x4 = {t: 1, v: 0}
    var x5 = {t: 1, v: 0}
    var x6 = {t: 1, v: 0}
    var m1 = {t: 3, l: c3, r: x1}
    var m2 = {t: 3, l: m1, r: x2}
    var m3 = {t: 3, l: m2, r: x3}
    var m4 = {t: 3, l: c2, r: x4}
    var m5 = {t: 3, l: m4, r: x5}
    var a1 = {t: 2, l: m3, r: m5}
    var a2 = {t: 2, l: a1, r: x6}
    var a3 = {t: 2, l: a2, r: c5}
    return a3
}

pn benchmark() {
    var result = 0
    var iter = 0
    while (iter < 5000) {
        var e = make_expr()
        var d = deriv(e)
        result = count_nodes(d)
        iter = iter + 1
    }
    return result
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 45) {
        print("deriv: PASS\n")
    } else {
        print("deriv: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
