// CW33 / S9.1.3: a typed `var` parameter that is REBOUND inside the callee
// publishes its final value through the caller's home, exactly like an
// untyped `var` (D8.1.1v10). Every scalar lane, string, record and their
// optional forms; rebinds inside branches and loops; a `let` snapshot taken
// before the call keeps its value (S9.1.2). Identical on interp, jit and auto.

type P = {x: int, y: int}

pn r_untyped(var s) { s = "u" }
pn r_str(var s: string) { s = s ++ "!" }
pn r_strq(var s: string?) { s = null }
pn r_int(var n: int) { n = n + 1 }
pn r_intq(var n: int?) { if (n == null) { n = 7 } else { n = null } }
pn r_float(var f: float) { f = f * 2.0 }
pn r_floatq(var f: float?) { f = null }
pn r_bool(var b: bool) { b = not b }
pn r_boolq(var b: bool?) { b = null }
pn r_rec(var p: P) { p = {x: p.x + 10, y: p.y} }
pn r_recq(var p: P?) { p = null }
pn r_arr(var a: int[]) { a = [9, 9] }

// the rebind is reached only on one path
pn r_branch(var n: int, take: bool) {
    if (take) { n = 100 }
}

// rebound repeatedly inside a loop, with a read between
pn r_loop(var n: int, k: int) {
    var i = 0
    while (i < k) { n = n * 2; i = i + 1 }
}

// a caller's own `var` parameter forwarded to a rebinding callee
pn r_chain(var n: int) {
    r_int(n)
    r_int(n)
}

// the rebound float lane keeps flowing into native arithmetic afterwards
pn scale_twice(f: float) float {
    var g: float = f
    r_float(g)
    r_float(g)
    return g + 0.5
}

pn main() {
    var a = "x"; r_untyped(a)
    var b: string = "x"; let b_keep = b; r_str(b)
    var c: string? = "x"; r_strq(c)
    var d: int = 1; let d_keep = d; r_int(d)
    var e: int? = null; r_intq(e)
    var e2: int? = 3; r_intq(e2)
    var f: float = 1.5; let f_keep = f; r_float(f)
    var g: float? = 2.5; r_floatq(g)
    var h: bool = false; r_bool(h)
    var h2: bool? = true; r_boolq(h2)
    var p: P = {x: 1, y: 2}; let p_keep = p; r_rec(p)
    var q: P? = {x: 1, y: 2}; r_recq(q)
    var arr: int[] = [1]; r_arr(arr)
    print([a, b, c, d, e, e2, f, g, h, h2, p, q, arr])
    print("\n")
    print([b_keep, d_keep, f_keep, p_keep])
    print("\n")

    var t: int = 1; r_branch(t, false)
    var u: int = 1; r_branch(u, true)
    var v: int = 1; r_loop(v, 5)
    var w: int = 1; r_chain(w)
    print([t, u, v, w, scale_twice(1.0)])
    print("\n")

    // repeated calls in a loop, so the auto tier promotes both sides
    var acc: int = 0
    var i = 0
    while (i < 20) {
        r_int(acc)
        i = i + 1
    }
    var fsum: float = 1.0
    var j = 0
    while (j < 8) {
        r_float(fsum)
        j = j + 1
    }
    print([acc, fsum])
    print("\n")
}
