// LR12-45 (S9.1.2, S9.1.3): a member place passed to a `pn` beside plain container
// arguments is borrowed in place. A plain argument that may share the place's storage
// (it reads the place's root, or a binding already holds the leaf) stays a snapshot.
pn bump(var l, extra) {
    l.n = l.n + 1
    l.seen = extra.n
}

pn both(var a, var b, extra) {
    a.n = a.n + extra.n
    b.n = b.n + extra.n
}

pn main() {
    // no alias: a plain container sibling from another root
    var s = {layer: {n: 1, seen: 0}, other: {n: 10}}
    bump(s.layer, s.other)^

    // the plain argument reads the place's root: it sees the leaf before the write
    var r = {layer: {n: 1, seen: 0}}
    bump(r.layer, r.layer)^

    // the whole root as the plain argument
    var u = {layer: {n: 1, seen: 0}, n: 5}
    bump(u.layer, u)^

    // a binding that already holds the leaf keeps its value
    var t = {layer: {n: 1, seen: 0}}
    let held = t.layer
    bump(t.layer, held)^

    // repeated batches write in place; a value kept before them is unchanged
    var v = {layer: {n: 0, seen: 0}}
    let before = v.layer
    for (i in 1 to 5) { bump(v.layer, s.other)^ }

    // two places with distinct roots and a plain sibling
    var p = {x: {n: 1}}
    var q = {y: {n: 2}}
    let k = {n: 100}
    both(p.x, q.y, k)^

    // a list place with a plain list sibling
    var w = {items: [1, 2, 3]}
    let extra = [7]
    bump_first(w.items, extra)^

    return {
        no_alias: s.layer, other: s.other,
        root_read: r.layer,
        whole_root: u.layer, whole_root_n: u.n,
        held_place: t.layer, held: held,
        repeated: v.layer, before: before,
        both_x: p.x, both_y: q.y, k: k,
        items: w.items, extra: extra
    }
}

pn bump_first(var xs, extra) {
    xs[0] = xs[0] + extra[0]
}
