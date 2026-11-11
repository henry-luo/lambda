// S9.1.2: a wrapper in another module keeps its argument only where its callee does. A
// value the callee keeps stays a snapshot after the caller writes its binding; a value it
// only reads is written in place by the caller.
import facade: .mod_retain_facade

pn main() {
    // kept through the wrapper: the kept copy does not see later writes
    var a = [1, 2, 3]
    let kept = facade.keep_via(a)
    a[0] = 9

    var m = {n: 1}
    let kept_map = facade.keep_via(m)
    m.n = 2

    // returned through the wrapper: the result does not see later writes
    var r = [4, 5]
    let back = facade.first_via(r)
    r[1] = 50

    // a part kept through the wrapper: the kept child does not see later writes
    var c = {child: {n: 1}, other: 2}
    let held = facade.part_via(c)
    c.child.n = 9

    // only read through the wrapper: later writes land in the binding
    var b = [1, 2, 3]
    var total = 0
    for (i in 0 to 2) {
        total = total + facade.size_via(b)^
        b[i] = b[i] * 10
    }

    return {kept: kept.held, a: a, kept_map: kept_map.held, m: m, back: back, r: r, held: held.inner, c: c.child, b: b, total: total}
}
