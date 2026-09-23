// A map literal bound to a named record contract may list its fields in any
// order: admission reifies it into the contract's declared layout, moving each
// field by name (D3.2.4v4). The fields had kept the literal's order while the
// contract's offsets read them -- `{b: "s", a: 1}` bound to `{a: int, b: string}`
// read `a` as 0 and crashed printing `b` -- and the JIT adopted the contract
// for the literal and filled its slots in source order.

type P = {a: int, b: string}
type Q = {x: float, y: int}
type R = {n: int?, s: string?}
type O = {p: P, k: int}
type N = {key: float, left: N?, right: N?}

fn take(p: P) { [p.a, p.b] }
fn give() P { {b: "r", a: 4} }
fn dyn(v) => v
fn insert(t: N?, k: float) N {
    if (t == null) {right: null, left: null, key: k}
    else if (k < t.key) {right: t.right, left: insert(t.left, k), key: t.key}
    else {left: t.left, right: insert(t.right, k), key: t.key}
}
fn walk(t: N?) { if (t == null) [] else [*walk(t.left), t.key, *walk(t.right)] }

pn main() {
    // plain, and printed in the contract's order
    let p: P = {b: "s", a: 1}
    print([p.a, p.b, p])
    print("\n")

    // an optional contract
    let q: P? = {b: "t", a: 2}
    print([q.a, q.b])
    print("\n")

    // a recursive record whose self fields are optional
    var x: N = {right: null, left: null, key: 2.5}
    print([x.key, x.left, x.right])
    print("\n")
    var t: N = {right: {right: null, key: 1.5, left: null}, left: null, key: 2.5}
    print([t.key, t.left, t.right.key, t.right.right])
    print("\n")

    // via a bound variable, and through an untyped function
    var r = {b: "u", a: 3}
    let s: P = r
    print([s.a, s.b])
    print("\n")
    let d: P = dyn({b: "d", a: 7})
    print([d.a, d.b])
    print("\n")

    // a conversion, a nullable lane and a nested record move with their field
    let cq: Q = {y: 2, x: 1}
    print([cq.x, cq.y, type(cq.x)])
    print("\n")
    let r1: R = {s: null, n: 5}
    let r2: R = {s: "a", n: null}
    print([r1.n, r1.s, r2.n, r2.s])
    print("\n")
    let o: O = {k: 1, p: {b: "x", a: 2}}
    print([o.k, o.p.a, o.p.b])
    print("\n")

    // parameters, returns, reassignment and a later field write
    print([take({b: "q", a: 9}), give().a, give().b])
    print("\n")
    var v: P = {a: 1, b: "s"}
    v = {b: "t", a: 2}
    v.a = 5
    print([v.a, v.b])
    print("\n")

    // a tree built from reordered literals
    var root: N? = null
    var i = 0
    while (i < 20) {
        root = insert(root, ((i * 7) % 20) * 1.5)
        i = i + 1
    }
    let keys = walk(root)
    print([len(keys), keys[0], keys[19]])
    print("\n")

    // a literal already in the contract's order
    let w: P = {a: 1, b: "s"}
    print([w.a, w.b, w])
    print("\n")
}
