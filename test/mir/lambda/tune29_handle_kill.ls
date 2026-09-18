// Tune29 §19.1 (D4.4.4v4, COW Appendix D): the named-handle kill rows. A
// place copy `var h = root.path` borrows its place only while nothing may
// observe an in-place write. Every probe prints what per-access snapshots
// would; the sidecar pins which binds lower to a handle.
type Row = {vals: array, n: int}
type Grid = {row: Row, tag: int}

pn fresh() Grid { return {row: {vals: [0, 0], n: 0}, tag: 0} }

// reads a part: a plain argument that keeps no root
pn first(r: Row) any { return r.vals[0] }
// keeps the root itself
pn same(r: Row) Row { return r }

// kept: the handle is read by a plain callee, then written and stored back
// in one arm (CW36 + plain read-only argument)
pn read_then_write(var g: Grid, flag: bool) any {
    var r = g.row
    var seen = first(r)
    if (flag) {
        r.n = r.n + 1
        g.row = r
    }
    return seen
}

// killed: the callee may return the handle's own root, so the bind snapshots
pn root_escapes(var g: Grid) Row {
    var r = g.row
    var kept = same(r)
    r.n = 9
    g.row = r
    return kept
}

// killed: the root is handed to a writer while the copy is alive
pn bump(var g: Grid) any { g.row.n = g.row.n + 100 }
pn root_to_var(var g: Grid) int {
    var r = g.row
    bump(g)
    r.n = r.n + 1
    g.row = r
    return g.row.n
}

pn main() {
    var g = fresh()
    var snap = g
    print(read_then_write(g, true)); print(" ")
    print(g.row.n); print(" ")
    print(snap.row.n); print("\n")
    var h = fresh()
    var kept = root_escapes(h)
    print(kept.n); print(" ")
    print(h.row.n); print("\n")
    var k = fresh()
    print(root_to_var(k)); print("\n")
}
