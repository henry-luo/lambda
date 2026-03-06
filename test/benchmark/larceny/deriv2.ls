// Larceny Benchmark: deriv (Typed version)
// Symbolic differentiation of expression trees
// Adapted from Larceny/Gambit benchmark suite
// Uses typed maps for direct field access optimization
// Differentiates 3*x^3 + 2*x^2 + x + 5, repeats 5000 times, counts nodes
// Expected: 45 nodes in derivative

// Expression tags: 0=const, 1=var(x), 2=add, 3=mul
// Unified shape: all nodes carry t, v, l, r (leaves set l/r to null)
type Expr = {t: int, v: int, l: map, r: map}

pn deriv(e: Expr) {
    if (e.t == 0) {
        var r: Expr = {t: 0, v: 0, l: null, r: null}
        return r
    }
    if (e.t == 1) {
        var r: Expr = {t: 0, v: 1, l: null, r: null}
        return r
    }
    if (e.t == 2) {
        var dl = deriv(e.l)
        var dr = deriv(e.r)
        var r: Expr = {t: 2, v: 0, l: dl, r: dr}
        return r
    }
    // t == 3: product rule  d(a*b) = a*db + da*b
    var dl = deriv(e.l)
    var dr = deriv(e.r)
    var p1: Expr = {t: 3, v: 0, l: e.l, r: dr}
    var p2: Expr = {t: 3, v: 0, l: dl, r: e.r}
    var r: Expr = {t: 2, v: 0, l: p1, r: p2}
    return r
}

pn count_nodes(e: Expr) {
    if (e.t == 0 or e.t == 1) {
        return 1
    }
    return 1 + count_nodes(e.l) + count_nodes(e.r)
}

pn make_expr() {
    // 3*x*x*x + 2*x*x + x + 5
    var c3: Expr = {t: 0, v: 3, l: null, r: null}
    var c2: Expr = {t: 0, v: 2, l: null, r: null}
    var c5: Expr = {t: 0, v: 5, l: null, r: null}
    var x1: Expr = {t: 1, v: 0, l: null, r: null}
    var x2: Expr = {t: 1, v: 0, l: null, r: null}
    var x3: Expr = {t: 1, v: 0, l: null, r: null}
    var x4: Expr = {t: 1, v: 0, l: null, r: null}
    var x5: Expr = {t: 1, v: 0, l: null, r: null}
    var x6: Expr = {t: 1, v: 0, l: null, r: null}
    var m1: Expr = {t: 3, v: 0, l: c3, r: x1}
    var m2: Expr = {t: 3, v: 0, l: m1, r: x2}
    var m3: Expr = {t: 3, v: 0, l: m2, r: x3}
    var m4: Expr = {t: 3, v: 0, l: c2, r: x4}
    var m5: Expr = {t: 3, v: 0, l: m4, r: x5}
    var a1: Expr = {t: 2, v: 0, l: m3, r: m5}
    var a2: Expr = {t: 2, v: 0, l: a1, r: x6}
    var a3: Expr = {t: 2, v: 0, l: a2, r: c5}
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
