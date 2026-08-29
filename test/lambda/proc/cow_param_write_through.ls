// Pins the CURRENT plain-param write-through pn ABI (flag off). When CW29
// plain-param snapshots flip on by default (S9.1.3, COW §11.9), this golden
// changes to "1 2 a 5" and the scripts the sweep flagged migrate to `var`.
// The flag-ON behavior is probed by temp/cw29/param_snap.ls (LR12-9 pattern).

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
