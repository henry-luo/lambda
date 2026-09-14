// S9.2.2 / S9.1.2: a builtin in-place mutator whose first argument is a PLACE
// (`push(m.a, v)`, `splice(arr[0], s, n)`, `set(m.v, k, v)`) borrows that place:
// the root and every link are detached before the write, so a second slot or a
// snapshot that held the same container never observes it. Identical on
// interp, jit and auto.

type Row = {tags: array, vals: int[]}

pn add_tag(var r: Row, t) {
    push(r.tags, t)
}

// a plain parameter's own writes are snapshots (S9.1.3)
pn local_push(m) {
    push(m.a, "local")
    return m.a
}

pn main() {
    // one array published into two member slots
    var m = {a: [], b: []}
    var x = ["q"]
    m.a = x
    m.b = x
    push(m.a, 1)
    print([m.a, m.b, x])
    print("\n")

    // the same through index places
    var arr = [[], []]
    var y = ["q"]
    arr[0] = y
    arr[1] = y
    push(arr[0], 1)
    print([arr[0], arr[1], y])
    print("\n")

    // a snapshot of the root, through a nested member/index/member place
    var n = {rows: [{tags: ["a"]}, {tags: []}]}
    let n_snap = n
    push(n.rows[0].tags, "b")
    print([n.rows[0].tags, n_snap.rows[0].tags])
    print("\n")

    // a typed int[] field keeps its checked append
    var tr: Row = {tags: [], vals: [1, 2]}
    let tr_snap = tr
    push(tr.vals, 3)
    print([tr.vals, tr_snap.vals])
    print("\n")

    // a var-parameter root writes through to the caller only
    var r: Row = {tags: ["x"], vals: [0]}
    let r_snap = r
    add_tag(r, "y")
    print([r.tags, r_snap.tags])
    print("\n")

    var p = {a: ["p"]}
    let returned = local_push(p)
    print([returned, p.a])
    print("\n")

    // repeated appends through one unique place
    var acc = {items: []}
    var i = 0
    while (i < 1000) {
        push(acc.items, i)
        i = i + 1
    }
    print([len(acc.items), acc.items[999]])
    print("\n")

    var s = {xs: ["p", "q", "r", "s"]}
    let s_snap = s
    splice(s.xs, 1, 2)
    print([s.xs, s_snap.xs])
    print("\n")

    var vm = {v: map()}
    let vm_snap = vm
    set(vm.v, "k", 1)
    print([vm.v, vm_snap.v])
    print("\n")

    // a place holding no array still yields push's own error value
    var bad = {n: 5}
    let err = push(bad.n, 1)
    print([type(err), bad.n])
    print("\n")
}
