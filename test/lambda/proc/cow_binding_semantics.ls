// S9.1.2: binding is a copy, for every container kind and in both directions.
// Under COW the copy is a share-mark; the first write through EITHER handle
// detaches, and no observer ever sees another's update (P6).

pn main() {
    // map alias, writer on the alias
    var m1 = {v: 1, w: 2}
    var m2 = m1
    m2.v = 99
    print(m1.v); print(" "); print(m2.v); print(" (map: ruled 1 99)\n")

    // map alias, writer on the ORIGINAL
    var m3 = {v: 5}
    var m4 = m3
    m3.v = 77
    print(m3.v); print(" "); print(m4.v); print(" (map reverse: ruled 77 5)\n")

    // rebind chain: three names, one storage, each write detaches once
    var c1 = {n: 0}
    var c2 = c1
    var c3 = c2
    c1.n = 1
    c2.n = 2
    c3.n = 3
    print(c1.n); print(" "); print(c2.n); print(" "); print(c3.n);
    print(" (chain: ruled 1 2 3)\n")

    // element alias: attribute and child stay independent
    var e1 = <p rank: 1, "draft">
    var e2 = e1
    e2.rank = 9
    e2[0] = "final"
    print(e1.rank); print(" "); print(e1[0]); print(" ");
    print(e2.rank); print(" "); print(e2[0]);
    print(" (element: ruled 1 draft 9 final)\n")

    // nested map: alias of the ROOT; deep write through the alias leaves the
    // original's whole subtree untouched (spine detach, O(depth))
    var t1 = {a: {b: {c: 1}}, side: {k: 5}}
    var t2 = t1
    t2.a.b.c = 42
    print(t1.a.b.c); print(" "); print(t2.a.b.c); print(" ");
    print(t2.side.k); print(" (spine: ruled 1 42 5)\n")

    // let source stays immutable through a var alias (the classic canary)
    let l1 = {v: 10}
    var l2 = l1
    l2.v = 0
    print(l1.v); print(" "); print(l2.v); print(" (let canary: ruled 10 0)\n")
}
