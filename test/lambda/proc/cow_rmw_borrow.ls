// CW34 (COW §11.11): a read-modify-write handle -- `var l = root.path`, written
// through, stored back to the same path, dead afterwards -- borrows its place
// in both tiers when the runtime spine is unshared. Every case below must be
// observably identical to the S9.1.2 snapshot semantics it replaces.
pn arr_set(var a, idx, val) {          // cd/havlak's 2-level handle store
    var i1 = idx % 16
    var i0 = idx div 16
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) { c1 = fill(16, null) }
    c1[i1] = val
    l0[i0] = c1
    a.l0 = l0
    return 0
}
pn arr_get(a, idx) {
    let c1 = a.l0[idx div 16]
    if (c1 == null) { return null }
    return c1[idx % 16]
}
pn read_between(var a) {               // root observed inside the region: snapshot stays
    var l = a.xs
    l[0] = 9
    let seen = a.xs[0]
    a.xs = l
    return seen
}
pn early_exit(var a, flag) {           // a return without the store-back: snapshot stays
    var l = a.xs
    l[0] = 7
    if (flag) { return -1 }
    a.xs = l
    return 1
}
pn escape(var a, var other) {          // handle captured elsewhere before the write
    var l = a.xs
    push(other, l)
    l[1] = 5
    a.xs = l
    return 0
}
pn shared_root(var a) {                // spine shared at run time: falls back to the snapshot bind
    var snap = a
    var l = a.xs
    l[2] = 3
    a.xs = l
    return snap.xs[2]
}
pn rebound(var a) {                    // handle rebound to a fresh container on one path
    var l = a.ys
    if (l == null) { l = fill(2, 0) }
    l[0] = l[0] + 11
    a.ys = l
    return 0
}
pn lanes(var a) {                      // numeric-array and map handles
    var v = a.nums
    v[1] = v[1] + 100
    a.nums = v
    var m = a.node
    m.left = 42
    a.node = m
    return 0
}
pn branch_store(var t, key) {          // intermediate store-back before an early return
    var vals = t.vals
    if (key == 1) { vals[0] = 100; t.vals = vals; return 100 }
    vals[0] = vals[0] + key
    t.vals = vals
    return len(t.vals)
}
pn loop_handle(var a, n) {             // handle written in a loop whose counter is not a key
    var l = a.xs
    var i = 0
    while (i < n) { l[i] = i * 10; i = i + 1 }
    a.xs = l
    return 0
}
pn main() {
    var a = { l0: fill(4, null), xs: [0, 0, 0], ys: null, nums: fill(3, 1), node: {left: 0, right: 0} }
    var k = 0
    while (k < 40) { arr_set(a, k * 3 % 60, k); k = k + 1 }
    var got = []
    var j = 0
    while (j < 60) { push(got, arr_get(a, j)); j = j + 1 }
    print(got, "\n")
    var other = []
    print(read_between(a), " ", a.xs, "\n")
    print(early_exit(a, true), " ", a.xs, " ", early_exit(a, false), " ", a.xs, "\n")
    print(escape(a, other), " ", a.xs, " ", other, "\n")
    print(shared_root(a), " ", a.xs, "\n")
    print(rebound(a), " ", a.ys, " ", rebound(a), " ", a.ys, "\n")
    print(lanes(a), " ", a.nums, " ", a.node, "\n")
    var t = {vals: [5]}
    print(branch_store(t, 1), " ", t.vals, " ", branch_store(t, 2), " ", t.vals, "\n")
    print(loop_handle(a, 3), " ", a.xs, "\n")
}
