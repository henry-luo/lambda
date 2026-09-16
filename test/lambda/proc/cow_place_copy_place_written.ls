// CW24v3 / D4.4.6 (S9.1.2): a place copy is share-marked at its bind iff the
// PLACE may be written while the copy is alive -- a write through the root in
// the copy's range, a `var` pass of the root, or the copy escaping. The copy's
// own mutation still marks (D4.4.4v2). Before this rule only a MUTATED copy
// marked, so every `old` below saw the later write.
type N = {kid: {n: int}}
type K = {v: int}
type T = {nodes: K[]}

pn direct_write(var r: N) {
    let old = r.kid
    r.kid.n = 7
    print(old.n) print(r.kid.n)
}
pn bump(var r: N) { r.kid.n = r.kid.n + 10 }
pn var_pass(var r: N) {
    let old = r.kid
    bump(r)
    print(old.n) print(r.kid.n)
}
pn rmw_alias(var r: N) {
    let old = r.kid
    var h = r.kid
    h.n = 7
    r.kid = h
    print(old.n) print(r.kid.n)
}
pn nested_if(var r: N, go: bool) {
    if (go) { let old = r.kid
        r.kid.n = 7
        print(old.n) }
    print(r.kid.n)
}
pn in_loop(var r: N) {
    let old = r.kid
    var i = 0
    while (i < 2) { r.kid.n = r.kid.n + 1
        i = i + 1 }
    print(old.n) print(r.kid.n)
}
pn get_v(t: T, i: int) int { let node = t.nodes[i]
    return node.v }
pn get_node(t: T, i: int) K { let node = t.nodes[i]
    return node }
pn write_after_last_use(var r: N) {
    let old = r.kid
    print(old.n)
    r.kid.n = 7
    print(r.kid.n)
}

pn main() {
    var a: N = {kid: {n: 3}}
    direct_write(a)
    var b: N = {kid: {n: 3}}
    var_pass(b)
    var c: N = {kid: {n: 3}}
    rmw_alias(c) print(c.kid.n)
    var d: N = {kid: {n: 3}}
    nested_if(d, true)
    var e: N = {kid: {n: 3}}
    in_loop(e)
    var u = {kid: {n: 3}}
    let old_u = u.kid
    u.kid.n = 7
    print(old_u.n)
    var w = {kid: {n: 3}}
    var old_w = w.kid
    w.kid.n = 7
    print(old_w.n)
    var t: T = {nodes: [{v: 1}, {v: 2}]}
    print(get_v(t, 0))
    t.nodes[0].v = 9
    print(get_v(t, 0))
    let got = get_node(t, 0)
    t.nodes[0].v = 4
    print(got.v) print(t.nodes[0].v)
    var f: N = {kid: {n: 3}}
    write_after_last_use(f)
}
