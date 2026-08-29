// S9.1.3 locality through CALL CHAINS. A plain param is a snapshot at EVERY
// hop; `var` is the sole write-through construct, and mixing the two obeys
// each boundary independently.

pn leaf_plain(m) { m.n = 111 }
pn leaf_var(var m) { m.n = 222 }

pn mid_plain_to_plain(m) { leaf_plain(m) }
pn mid_plain_to_var(m) { leaf_var(m) }
pn mid_var_to_var(var m) { leaf_var(m) }
pn mid_var_to_plain(var m) { leaf_plain(m); m.n = m.n + 6 }

pn out_param(var acc, k: int) { acc.sum = acc.sum + k }
fn observe(m) int { m.n }

pn main() {
    // plain -> plain: nothing escapes either hop
    var a = {n: 1}
    mid_plain_to_plain(a)
    print(a.n); print(" (plain->plain: ruled 1)\n")

    // plain -> var: the borrow mutates the MIDDLE frame's snapshot, which
    // dies with the frame; the caller never sees it
    var b = {n: 1}
    mid_plain_to_var(b)
    print(b.n); print(" (plain->var: ruled 1)\n")

    // var -> var: a genuine borrow chain, fully visible
    var c = {n: 1}
    mid_var_to_var(c)
    print(c.n); print(" (var->var: ruled 222)\n")

    // var -> plain: the leaf write stays in the leaf (m.n is still 1 when
    // the middle reads it back); the middle's own borrow write reaches us
    var d = {n: 1}
    mid_var_to_plain(d)
    print(d.n); print(" (var->plain: ruled 7)\n")

    // the out-param idiom that replaces write-through accumulators
    var acc = {sum: 0}
    out_param(acc, 5)
    out_param(acc, 7)
    print(acc.sum); print(" (out-param: ruled 12)\n")

    // fn readers never join the mutation story at all
    var e = {n: 42}
    let seen = observe(e)
    print(seen); print(" (fn read: ruled 42)\n")
}
