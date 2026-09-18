// D4.4.4v2: read-modify-write handles on SIBLING fields of one `var` root may
// each borrow their place, and a return that precedes every use of a handle
// needs no store-back. Every case below must print what snapshot semantics
// (S9.1.2, S9.1.3) print, on every tier.

type Table = {keys: array, vals: array}

pn put_slot(var t: Table, key: int, value: any) any {
    var keys = t.keys
    var vals = t.vals
    var i = 0
    while (i < len(keys)) {
        if (keys[i] == key) {
            var old = vals[i]
            vals[i] = value
            t.vals = vals
            return old
        }
        i = i + 1
    }
    push(keys, key)
    push(vals, value)
    t.keys = keys
    t.vals = vals
    return null
}

// the handle is written before the early return, which has no store-back:
// the write must not reach the caller (the borrow is refused)
pn put_discard(var t: Table, key: int, stop: bool) int {
    var keys = t.keys
    push(keys, key)
    if (stop) {
        return 0
    }
    t.keys = keys
    return 1
}

// a sibling store of the handle itself would alias it into two slots
pn cross_store(var t: Table, key: int) int {
    var keys = t.keys
    push(keys, key)
    t.vals = keys
    t.keys = keys
    return len(keys)
}

pn run_hot(var t: Table) {
    var k = 0
    while (k < 60) {
        put_slot(t, k % 20, k)
        k = k + 1
    }
}

pn main() {
    var t: Table = {keys: [], vals: []}
    run_hot(t)
    print([len(t.keys), len(t.vals), t.keys[3], t.vals[3], t.vals[19]])
    print("\n")

    // a snapshot of the root before the puts keeps its own arrays
    let before = t
    put_slot(t, 100, "x")
    put_slot(t, 3, "y")
    print([len(before.keys), len(t.keys), before.vals[3], t.vals[3], t.vals[20]])
    print("\n")

    // one shared array in both slots: writing through `keys` must not show
    // up in `vals`
    var shared = ["a", "b"]
    var s: Table = {keys: shared, vals: shared}
    put_slot(s, 7, 8)
    print([s.keys, s.vals, shared])
    print("\n")

    var d: Table = {keys: ["k"], vals: ["v"]}
    print([put_discard(d, 5, true), d.keys, put_discard(d, 6, false), d.keys])
    print("\n")

    var c: Table = {keys: ["k"], vals: []}
    let n = cross_store(c, 2)
    print([n, c.keys, c.vals])
    print("\n")

    // an alias published by field stores is share-marked by the store, so a
    // borrowed sibling handle still detaches its leaf
    var u: Table = {keys: [], vals: []}
    var y = ["b"]
    u.keys = y
    u.vals = y
    put_slot(u, 9, 10)
    print([u.keys, u.vals, y])
    print("\n")
}
