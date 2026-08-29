// Pins the SHIPPED plain-param snapshot semantics (S9.1.3/CW29, default ON
// since the 2026-08-29 flip): writes through plain params stay local to the
// procedure; `var` is the sole write-through construct. The pre-flip
// write-through ABI ("9 9 9 5") is reachable only via the temporary
// LAMBDA_COW_CAPTURE=0 escape hatch.

pn set_flat(m) { m.cur = 9 }
pn set_nested(t) { t.a[1] = 9 }
pn set_arr(xs) { xs[0] = 9 }
pn read_only(m) { m.cur + 1 }

pn main() {
    var m = {cur: 1}
    set_flat(m)
    print(m.cur); print(" ")

    var t = {a: [1, 2, 3]}
    set_nested(t)
    print(t.a[1]); print(" ")

    var xs = ["a", "b"]
    set_arr(xs)
    print(xs[0]); print(" ")

    // non-mutating callee: no mark, caller value untouched either way
    var r = {cur: 5}
    read_only(r)
    print(r.cur); print("\n")
}
